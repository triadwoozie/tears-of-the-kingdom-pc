#!/usr/bin/env python3
"""TotK-Recomp ExeFS Decryptor & Extractor.

Extracts TotK v1.4.3 ExeFS modules (main, subsdk0, sdk, rtld, main.npdm)
from local/rom/update.nsp using local/keys/prod.keys into work/exefs/.
Strictly excluded from git tracking.
"""

from Cryptodome.Cipher import AES
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
import hashlib
import io
import os
from pathlib import Path
import struct
import sys

from ingest_rom import parse_pfs0


def load_keys(keys_path: Path):
    keys = {}
    for line in open(keys_path):
        line = line.strip()
        if '=' in line:
            k, v = line.split('=', 1)
            keys[k.strip()] = bytes.fromhex(v.strip())
    return keys


def decrypt_header(raw, key):
    out = bytearray()
    for sector in range(6):
        cipher = Cipher(algorithms.AES(key), modes.XTS(sector.to_bytes(16, 'big'))).decryptor()
        out += cipher.update(raw[sector * 512:(sector + 1) * 512]) + cipher.finalize()
    return out


def extract_exefs(update_nsp_path: Path, keys_path: Path, output_dir: Path):
    print(f"[Extract] Loading keys from {keys_path}...")
    keys = load_keys(keys_path)

    print(f"[Extract] Parsing {update_nsp_path}...")
    with open(update_nsp_path, 'rb') as f:
        nsp_entries = parse_pfs0(f)

        # Locate ticket
        tik_name = next(k for k in nsp_entries if k.endswith('.tik'))
        tik_off, _ = nsp_entries[tik_name]
        f.seek(tik_off + 0x180)
        enc_title_key = f.read(16)
        title_key = AES.new(keys['titlekek_0f'], AES.MODE_ECB).decrypt(enc_title_key)
        print(f"  Decrypted Title Key: {title_key.hex()}")

        # Locate Program NCA
        prog_nca = next(k for k in nsp_entries if k.endswith('.nca') and not k.endswith('.cnmt.nca'))
        nca_off, _ = nsp_entries[prog_nca]

        f.seek(nca_off)
        hdr = decrypt_header(f.read(0xC00), keys['header_key'])
        fs0 = hdr[0x400:0x600]

        sec0_start_blk = struct.unpack_from('<I', hdr, 0x240)[0]
        sec0_offset = sec0_start_blk * 512
        pfs0_off = struct.unpack_from('<Q', fs0, 8 + 0x38)[0]
        pfs0_sz = struct.unpack_from('<Q', fs0, 8 + 0x40)[0]

        target_off = sec0_offset + pfs0_off
        section_ctr = fs0[0x140:0x148]

        # Read PFS0 header to get file table
        f.seek(nca_off + target_off)
        enc_pfs0_hdr = f.read(0x1000)

        ofs_val = target_off >> 4
        ctr = bytearray(16)
        for j in range(8):
            ctr[j] = section_ctr[8 - j - 1]
            ctr[16 - j - 1] = (ofs_val >> (8 * j)) & 0xFF

        dec_pfs0_hdr = AES.new(title_key, AES.MODE_CTR, initial_value=bytes(ctr), nonce=b'').decrypt(enc_pfs0_hdr)
        pfs_files = parse_pfs0(io.BytesIO(dec_pfs0_hdr))

        output_dir.mkdir(parents=True, exist_ok=True)
        print(f"[Extract] Extracting {len(pfs_files)} ExeFS modules to {output_dir}...")

        for name, (file_rel_off, file_sz) in pfs_files.items():
            out_file = output_dir / name
            print(f"  -> Extracting '{name}' ({file_sz:,} bytes)...", flush=True)

            abs_file_off = target_off + file_rel_off
            f.seek(nca_off + abs_file_off)

            block_ofs = abs_file_off >> 4
            byte_skip = abs_file_off & 0x0F

            file_ctr = bytearray(16)
            for j in range(8):
                file_ctr[j] = section_ctr[8 - j - 1]
                file_ctr[16 - j - 1] = (block_ofs >> (8 * j)) & 0xFF

            cipher = AES.new(title_key, AES.MODE_CTR, initial_value=bytes(file_ctr), nonce=b'')
            if byte_skip:
                cipher.decrypt(b'\x00' * byte_skip)

            hasher = hashlib.sha256()
            with open(out_file, 'wb') as out_f:
                remaining = file_sz
                while remaining > 0:
                    to_read = min(remaining, 1024 * 1024)
                    chunk = f.read(to_read)
                    dec_chunk = cipher.decrypt(chunk)
                    out_f.write(dec_chunk)
                    hasher.update(dec_chunk)
                    remaining -= len(chunk)

            digest = hasher.hexdigest()
            print(f"     Extracted: {out_file.name} (SHA-256: {digest})")

    print("[Extract] ExeFS extraction completed successfully!")


if __name__ == '__main__':
    extract_exefs(
        Path("local/rom/update.nsp"),
        Path("local/keys/prod.keys"),
        Path("work/exefs")
    )
