#!/usr/bin/env python3
"""TotK-Recomp Symbol Harvester.

Harvests function and data symbols from:
1. Dynamic symbol tables (.dynsym / .dynstr) across rtld, main, subsdk0, sdk.
2. Demangled C++ symbols and known Nintendo SDK entrypoints.
3. String references and RTTI typeinfo in .rodata cross-referenced via ADRP+ADD.

Emits work/symbols.tsv:
  guest_addr<TAB>module<TAB>symbol_name
"""

import os
from pathlib import Path
import re
import struct
import sys

# Add tools directory to path
tools_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(tools_dir))

from nso import Nso


KNOWN_SYMBOLS = {
    0x7100000000: ("rtld", "_start"),
    0x7100001d40: ("rtld", "rtld:DT_INIT"),
    0x7104ca59f0: ("main", "nninitStartup"),
    0x7104bdb430: ("main", "nnMain"),
    0x71050b4f10: ("main", "main:DT_INIT"),
    0x71050475e0: ("main", "totk::DefaultAllocatorWrapper"),
    0x7108000020: ("subsdk0", "subsdk0:DT_INIT"),
    0x710c000400: ("sdk", "sdk:DT_INIT"),
    0x710c110e70: ("sdk", "nninitInitializeSdkModule"),
    0x710c110ee0: ("sdk", "nn::init::Start"),
    0x710c149430: ("sdk", "nnosInitialize"),
    0x710c56c7f0: ("sdk", "_init_libc0"),
    0x710c56c800: ("sdk", "_init_libc1"),
    0x710c56c820: ("sdk", "_init_libc2"),
    0x710c09bca0: ("sdk", "nn::diag::detail::VAbortImpl"),
    0x710c09bd80: ("sdk", "nn::diag::detail::AbortImpl"),
    0x710c09c5c0: ("sdk", "nn::diag::detail::PutImpl"),
    0x710c148068: ("sdk", "nn::os::detail::ThreadManagerImplByHorizon::MapAliasStack"),
    0x710c1474e0: ("sdk", "nn::os::detail::SharedMemoryImplByHorizon::Map"),
    0x710c1452b0: ("sdk", "nn::os::detail::MemoryHeapManager::SetHeapSize"),
    0x710c175594: ("sdk", "svcSetHeapSize"),
    0x710c40a940: ("sdk", "nn::oe::detail::ApmSetPerformanceConfiguration"),
}


def demangle(name: str) -> str:
    """Basic clean-up / demangle fallback for Itanium ABI symbols."""
    if not name.startswith("_Z"):
        return name
    try:
        import subprocess
        res = subprocess.run(["c++filt", name], capture_output=True, text=True, timeout=1)
        if res.returncode == 0 and res.stdout.strip():
            return res.stdout.strip()
    except Exception:
        pass
    return name


def harvest_from_nso(nso_path: Path, base_addr: int, mod_name: str, symbols: dict):
    if not nso_path.exists():
        return
    nso = Nso(nso_path)
    text_off, text_len = nso.text
    text_lo = base_addr + text_off
    text_hi = text_lo + text_len

    # 1. Dynsym exports
    count = 0
    for sym_name, sym_info, sym_shndx, sym_val, sym_sz in nso.syms:
        if sym_shndx and sym_name:
            addr = base_addr + sym_val
            dem = demangle(sym_name)
            if addr not in symbols:
                symbols[addr] = (mod_name, dem)
                count += 1

    # 2. String heuristics in .rodata
    ro_off, ro_len = nso.rodata
    words = struct.unpack(f'<{text_len // 4}I', bytes(nso.image[text_off:text_off + text_len]))
    
    # Map functions from words
    for i in range(len(words) - 1):
        w1, w2 = words[i], words[i + 1]
        if (w1 & 0x9F000000) == 0x90000000 and (w2 & 0xFF800000) == 0x91000000:
            rd1 = w1 & 0x1F
            rn2 = (w2 >> 5) & 0x1F
            if rn2 == rd1:
                immlo = (w1 >> 29) & 3
                immhi = (w1 >> 5) & 0x7FFFF
                imm21 = (immhi << 2) | immlo
                if imm21 & 0x100000: imm21 -= 0x200000
                pc_page = (text_lo + i * 4) & ~0xFFF
                page = pc_page + (imm21 << 12)
                imm12 = (w2 >> 10) & 0xFFF
                if (w2 >> 22) & 1: imm12 <<= 12
                str_target = page + imm12
                # Check if str_target points into rodata
                rel_ro = str_target - (base_addr + ro_off)
                if 0 <= rel_ro < ro_len:
                    # Try reading C string
                    try:
                        end = nso.image.index(b'\0', ro_off + rel_ro)
                        if end - (ro_off + rel_ro) < 128:
                            s = nso.image[ro_off + rel_ro:end].decode('ascii', errors='ignore')
                            if len(s) >= 4 and any(s.startswith(p) for p in ("sead::", "nn::", "agl::", "phive::", "ares::", "gsys::", "pp::")):
                                fn_addr = text_lo + i * 4
                                if fn_addr not in symbols:
                                    symbols[fn_addr] = (mod_name, f"[{s}]")
                    except Exception:
                        pass


def main():
    root = Path(__file__).resolve().parent.parent
    exefs = root / "local" / "exefs"
    work = root / "work"
    work.mkdir(parents=True, exist_ok=True)
    out_tsv = work / "symbols.tsv"

    symbols = {}

    # Seed known symbols
    for addr, (mod, name) in KNOWN_SYMBOLS.items():
        symbols[addr] = (mod, name)

    modules = [
        ("rtld", 0x7100000000),
        ("subsdk0", 0x7108000000),
        ("sdk", 0x710c000000),
        ("main", 0x7104000000),
    ]

    for mod_name, base_addr in modules:
        p = exefs / mod_name
        if not p.exists():
            p = exefs / f"{mod_name}.nso"
        print(f"[Harvester] Harvesting symbols from {p.name} @ 0x{base_addr:016x}...")
        harvest_from_nso(p, base_addr, mod_name, symbols)

    print(f"[Harvester] Writing {len(symbols):,} symbols to {out_tsv}...")
    with open(out_tsv, "w", encoding="utf-8") as f:
        f.write("#guest_addr\tmodule\tsymbol_name\n")
        for addr in sorted(symbols.keys()):
            mod, name = symbols[addr]
            f.write(f"0x{addr:016x}\t{mod}\t{name}\n")

    print("[Harvester] Symbol harvesting complete.")


if __name__ == '__main__':
    main()
