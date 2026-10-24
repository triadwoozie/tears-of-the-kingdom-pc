#!/usr/bin/env python3
"""TotK-Recomp ROM & ExeFS Ingestion Tool.

Validates user-provided game packages or extracted folders against
The Legend of Zelda: Tears of the Kingdom v1.4.3 module fingerprints.
Zero proprietary keys or assets are bundled.
"""

import argparse
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


def inspect_path(path: Path) -> dict:
    """Inspect a directory or file and return detected status."""
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
            # Check for direct module files or exefs folder
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
                    result['modules'][name] = {
                        'path': str(target),
                        'sha256': digest,
                        'matches': digest == expected_hash
                    }
                else:
                    result['modules'][name] = {'path': None, 'sha256': None, 'matches': False}

            result['valid_v143'] = all(
                info['matches'] for info in result['modules'].values()
            )

        elif path.is_file():
            # Attempt to read PFS0 / NSP header
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
    parser.add_argument('--json', action='store_true', help="Output results in JSON format")
    args = parser.parse_args()

    if not args.path:
        # Default check against work/exefs
        default_work = Path("work/exefs")
        res = inspect_path(default_work)
    else:
        res = inspect_path(Path(args.path))

    if args.json:
        print(json.dumps(res, indent=2))
    else:
        print(f"=== TotK-Recomp ROM Ingestion Status ===")
        print(f"Target Path: {res['path']}")
        print(f"Exists:      {res['exists']}")
        if res.get('error'):
            print(f"Notice:      {res['error']}")
        if res['modules']:
            print("Detected ExeFS Modules:")
            for mod, data in res['modules'].items():
                status = "MATCH [v1.4.3]" if data['matches'] else ("MISMATCH" if data['sha256'] else "NOT FOUND")
                print(f"  - {mod:8s}: {status} (hash: {data['sha256'] or 'none'})")
        print(f"v1.4.3 Verified: {'YES' if res['valid_v143'] else 'NO'}")


if __name__ == '__main__':
    main()
