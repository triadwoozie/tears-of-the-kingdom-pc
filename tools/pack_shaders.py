#!/usr/bin/env python3
"""Pack the translated shaders into one file: <dir>/shaders.pack, read by src/gfx/vkb_shader.c.

usage: python tools/pack_shaders.py [<dir>]   (default data/shaders/ryujinx)

The translation writes two files per shader, <key>.spv and <key>.res.json (~57,000 files for 1.4.3); the game reads
the pack first and falls back to them. The loose files stay: the next translation run (--new-only) adds to them, and
the pack is written again. Layout, little-endian: b'UTSHPK01', u32 count, u32 0, then count entries sorted by key
{key[32] (the SHA-256), u64 offset, u32 SPIR-V bytes, u32 reflection bytes}, then the data (each SPIR-V followed by
its reflection).
"""
import os
import pathlib
import re
import struct
import sys

MAGIC = b'UTSHPK01'
KEY = re.compile(r'[0-9a-f]{64}')


def pack(folder, out=None, progress_cb=None):
    folder = pathlib.Path(folder)
    out = pathlib.Path(out) if out else folder / 'shaders.pack'
    if not folder.is_dir():
        print(f"Error: shader directory '{folder}' not found", file=sys.stderr)
        return 0, 0

    keys = sorted(p.stem for p in folder.glob('*.spv')
                  if KEY.fullmatch(p.stem) and (folder / f'{p.stem}.res.json').is_file())
    
    total = len(keys)
    if total == 0:
        print(f"Notice: No loose .spv/.res.json shaders found in '{folder}' to pack.", file=sys.stderr)
        # Create empty valid pack if requested
        tmp = out.with_name(out.name + '.tmp')
        with open(tmp, 'wb') as f:
            f.write(MAGIC + struct.pack('<II', 0, 0))
        os.replace(tmp, out)
        return 0, 16

    tmp = out.with_name(out.name + '.tmp')
    offset = 16 + 48 * total
    index = []
    print(f"Packing {total} shaders into {out}...")
    
    with open(tmp, 'wb') as f:
        f.write(MAGIC + struct.pack('<II', total, 0))
        f.write(bytes(48 * total))   # placeholder index

        for i, key in enumerate(keys):
            spv = (folder / f'{key}.spv').read_bytes()
            refl = (folder / f'{key}.res.json').read_bytes()
            f.write(spv)
            f.write(refl)
            index.append(bytes.fromhex(key) + struct.pack('<QII', offset, len(spv), len(refl)))
            offset += len(spv) + len(refl)

            if progress_cb and (i % 2500 == 0 or i == total - 1):
                progress_cb(i + 1, total)
            elif i % 5000 == 0 or i == total - 1:
                pct = int((i + 1) / total * 100)
                print(f"Progress: {i + 1}/{total} ({pct}%)")

        f.seek(16)
        f.write(b''.join(index))
        
    os.replace(tmp, out)
    return total, offset


def stale(folder, out=None):
    """True if the pack is missing or older than a translated shader (the translation added or rewrote one)."""
    folder = pathlib.Path(folder)
    out = pathlib.Path(out) if out else folder / 'shaders.pack'
    if not out.is_file():
        return True
    t = out.stat().st_mtime
    return any(e.stat().st_mtime > t for e in os.scandir(folder) if e.name.endswith(('.spv', '.res.json')))


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else 'data/shaders/ryujinx'
    n, size = pack(folder)
    print(f'{n} shaders packed into {pathlib.Path(folder) / "shaders.pack"} ({size / 2**20:.1f} MiB)')


if __name__ == '__main__':
    main()
