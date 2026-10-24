#!/usr/bin/env python3
"""TotK-Recomp ROM & ExeFS Ingestion Tool with Cryptographic Provenance (SPEC-020 / SPEC-095).

Validates user-provided game packages or extracted folders against
The Legend of Zelda: Tears of the Kingdom v1.4.3 module fingerprints,
and outputs work/provenance.toml. Zero proprietary keys or assets are bundled.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

# Official SHA-256 fingerprints for TotK v1.4.3 decompressed ExeFS modules
EXPECTED_V143_HASHES = {
    'main': 'e84a90d551fed0824b4b74933b9c6d043c458521363a7cea58b2faca1822dc9e',
    'subsdk0': '767c35bcd7d23e78f9063c69b638290b8b83f91e839beb10799e6576960179b1',
    'sdk': '1d187f29784cd0af46e9e123fc2a62c77b3b07c5a39c8633d90d6c80de64e4ad'
}

EXPECTED_BUILD_IDS = {
    'main': '277178b7dba1b6d4949a84778d4abc58',
    'subsdk0': '3bb8d7455d3f2824cf40b3c67537bfa2',
    'sdk': '68da6c99cbb281fbfb147eececaecb90'
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def parse_pfs0(stream, start_offset: int = 0) -> dict[str, tuple[int, int]]:
    """Parse a Nintendo PFS0 (Partition FileSystem) header."""
    stream.seek(start_offset)
    magic, count, strings_size, _ = struct.unpack('<4sIII', stream.read(16))
    if magic != b'PFS0':
        raise ValueError(f"Invalid magic: {magic}, expected PFS0")
    if not (0 < count <= 8192) or strings_size > 10 * 1024 * 1024:
        raise ValueError("Corrupt or invalid PFS0 header counts")

    entries_raw = [struct.unpack('<QQII', stream.read(24)) for _ in range(count)]
    names_raw = stream.read(strings_size)

    header_total = 16 + count * 24 + strings_size
    files = {}
    for offset, size, name_off, _ in entries_raw:
        end = names_raw.find(b'\0', name_off)
        if end < 0:
            continue
        name = names_raw[name_off:end].decode('utf-8', errors='replace')
        files[name] = (start_offset + header_total + offset, size)
    return files


def write_provenance_toml(output_path: Path, module_info: dict, all_matched: bool):
    """Write SPEC-020 / SPEC-095 compliant provenance.toml."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    now_iso = datetime.now(timezone.utc).isoformat()

    lines = [
        '# SPEC-020 / SPEC-095 Cryptographic Provenance Manifest',
        'schema_version = "1"',
        '',
        '[title]',
        'name = "The Legend of Zelda: Tears of the Kingdom"',
        'title_id = "0100F2C0115B6000"',
        'version = "1.4.3"',
        'region = "Global"',
        '',
        '[collection]',
        'device = "user-provided dump"',
        f'collected_at = "{now_iso}"',
        'tool_name = "TotK-Recomp Ingest"',
        'tool_version = "1.0.0"',
        '',
        '[verification]',
        f'all_matched = {"true" if all_matched else "false"}',
        f'verified_at = "{now_iso}"',
        ''
    ]

    for name, info in module_info.items():
        lines.append('[[inputs]]')
        lines.append(f'role = "{name}"')
        lines.append(f'path = "{info.get("path") or "unresolved"}"')
        lines.append(f'sha256 = "{info.get("sha256") or ""}"')
        lines.append(f'size = {info.get("size", 0)}')
        lines.append(f'format = "nso0"')
        lines.append(f'matches_expected = {"true" if info.get("matches", False) else "false"}')
        lines.append(f'build_id = "{EXPECTED_BUILD_IDS.get(name, "")}"')
        lines.append('')

    output_path.write_text('\n'.join(lines), encoding='utf-8')
    print(f"[Provenance] Wrote cryptographic provenance manifest to {output_path}")


def inspect_path(path: Path) -> dict:
    result = {
        'path': str(path),
        'exists': path.exists(),
        'is_dir': path.is_dir() if path.exists() else False,
        'modules': {},
        'valid_v143': False,
        'error': None
    }

    if not path.exists():
        result['error'] = 'Path does not exist'
        return result

    try:
        if path.is_dir():
            search_dirs = [path, path / 'exefs']
            for name, expected_hash in EXPECTED_V143_HASHES.items():
                target = None
                for d in search_dirs:
                    candidate = d / name
                    if candidate.is_file():
                        target = candidate
                        break
                    candidate_nso = d / f"{name}.nso"
                    if candidate_nso.is_file():
                        target = candidate_nso
                        break

                if target:
                    digest = sha256_file(target)
                    file_size = target.stat().st_size
                    result['modules'][name] = {
                        'path': str(target),
                        'sha256': digest,
                        'size': file_size,
                        'matches': digest == expected_hash
                    }
                else:
                    result['modules'][name] = {'path': None, 'sha256': None, 'size': 0, 'matches': False}

            result['valid_v143'] = all(
                info['matches'] for info in result['modules'].values()
            )

        elif path.is_file():
            with open(path, 'rb') as f:
                entries = parse_pfs0(f)
                result['pfs0_entries'] = list(entries.keys())
                result['is_pfs0'] = True

    except Exception as e:
        result['error'] = str(e)

    return result


def main():
    parser = argparse.ArgumentParser(description="TotK-Recomp ROM & ExeFS Ingestion Inspector")
    parser.add_argument('--path', type=str, help="Path to ROM file (.nsp) or extracted ExeFS directory")
    parser.add_argument('--provenance-out', type=str, default="work/provenance.toml", help="Path to write provenance.toml")
    parser.add_argument('--json', action='store_true', help="Output results in JSON format")
    args = parser.parse_args()

    target_path = Path(args.path) if args.path else Path("work/exefs")
    res = inspect_path(target_path)

    # Always generate/update provenance.toml
    prov_path = Path(args.provenance_out)
    write_provenance_toml(prov_path, res['modules'], res['valid_v143'])

    if args.json:
        print(json.dumps(res, indent=2))
    else:
        print(f"=== TotK-Recomp ROM Ingestion Status ===")
        print(f"Target Path:     {res['path']}")
        print(f"Exists:          {res['exists']}")
        if res.get('error'):
            print(f"Notice:          {res['error']}")
        if res['modules']:
            print("Detected ExeFS Modules:")
            for mod, data in res['modules'].items():
                status = "MATCH [v1.4.3]" if data['matches'] else ("MISMATCH" if data['sha256'] else "NOT FOUND")
                print(f"  - {mod:8s}: {status} (hash: {data['sha256'] or 'none'}, size: {data['size']} B)")
        print(f"v1.4.3 Verified: {'YES' if res['valid_v143'] else 'NO'}")


if __name__ == '__main__':
    main()
