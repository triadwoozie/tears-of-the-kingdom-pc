#!/usr/bin/env python3
"""TotK-Recomp Game Library Builder.

Orchestrates static recompilation across TotK v1.4.3 modules (rtld, main, subsdk0, sdk),
generates chunked C code, creates a parallel Ninja build manifest, and links build/libtotk_game.so.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

from lift_nso import lift_module, FN_HEADER


def generate_ninja_manifest(ninja_path: Path, c_files: list, lib_out: Path, root_dir: Path):
    rules = [
        "ninja_required_version = 1.5",
        "",
        "rule cc",
        f"  command = clang -O0 -fPIC -fno-strict-aliasing -Wno-unused-variable -Wno-unused-label -Wno-parentheses-equality -Wno-constant-logical-operand -I{root_dir}/src -I{root_dir}/src/runtime -I{root_dir}/third_party/simde -c $in -o $out",
        "  description = CC $out",
        "",
        "rule link",
        "  command = clang -shared -fPIC -Wl,--no-keep-memory -Wl,--discard-locals -fvisibility=hidden -o $out $in",
        "  description = LINK $out",
        ""
    ]

    build_targets = []
    obj_files = []

    for c_file in c_files:
        obj_file = c_file.with_suffix('.o')
        obj_files.append(obj_file)
        build_targets.append(f"build {obj_file}: cc {c_file}")

    build_targets.append("")
    build_targets.append(f"build {lib_out}: link {' '.join(str(o) for o in obj_files)}")
    build_targets.append("")

    ninja_path.write_text('\n'.join(rules + build_targets))
    print(f"[Builder] Generated Ninja build manifest: {ninja_path} ({len(c_files)} translation units)")


def build_game_library(exefs_dir: Path, out_dir: Path, lib_out: Path, max_main_fns: int = 1000, jobs: int = 2):
    t0 = time.time()
    print("=====================================================")
    print("  TotK-Recomp Game Library Builder (Phase 8)")
    print("=====================================================")

    out_dir.mkdir(parents=True, exist_ok=True)
    lib_out.parent.mkdir(parents=True, exist_ok=True)

    modules = [
        ('rtld', 0x7100000000, 0, 250),        # 36 functions (1 chunk)
        ('subsdk0', 0x7108000000, 0, 1000),    # 13,933 functions (14 chunks)
        ('sdk', 0x710c000000, 0, 1000),        # 40,514 functions (41 chunks)
        ('main', 0x7104000000, 0, 1000)        # 228,161 functions (229 chunks)
    ]

    all_fn_addrs = []
    all_c_files = []

    for mod_name, base_addr, fn_limit, c_size in modules:
        mod_path = exefs_dir / mod_name
        if not mod_path.exists():
            mod_path = exefs_dir / f"{mod_name}.nso"
        if not mod_path.exists():
            print(f"[Builder Error] Missing module: {mod_path}")
            continue

        fn_addrs, c_files = lift_module(mod_path, out_dir, base=base_addr, chunk_size=c_size, max_fns=fn_limit)
        all_fn_addrs.extend([(addr, mod_name) for addr in fn_addrs])
        all_c_files.extend(c_files)

    # Master registration table
    func_table_path = out_dir / "func_table.c"
    lines = [
        FN_HEADER,
        '#include <stdint.h>',
        'void register_recompiled_func(uint64_t guest_addr, GuestFn fn);',
        ''
    ]

    for addr, _ in all_fn_addrs:
        lines.append(f"void f_{addr:x}(Cpu *c);")

    lines.append("")
    lines.append("struct FuncEntry { uint64_t addr; GuestFn fn; };")
    lines.append(f"static const struct FuncEntry s_recompiled_funcs[{len(all_fn_addrs)}] = {{")
    for addr, _ in all_fn_addrs:
        lines.append(f"    {{ 0x{addr:x}ull, (GuestFn)f_{addr:x} }},")
    lines.append("};")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append('extern "C" {')
    lines.append("#endif")
    lines.append("")
    lines.append('__attribute__((visibility("default")))')
    lines.append("void register_recompiled_functions(void) {")
    lines.append(f"    // Registering {len(all_fn_addrs):,} recompiled guest functions")
    lines.append("    for (size_t i = 0; i < sizeof(s_recompiled_funcs) / sizeof(s_recompiled_funcs[0]); ++i) {")
    lines.append("        register_recompiled_func(s_recompiled_funcs[i].addr, s_recompiled_funcs[i].fn);")
    lines.append("    }")
    lines.append("}")
    lines.append("")
    lines.append('__attribute__((visibility("default")))')
    lines.append("uint64_t get_recompiled_function_count(void) {")
    lines.append(f"    return {len(all_fn_addrs)}ull;")
    lines.append("}")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append("}")
    lines.append("#endif")

    func_table_path.write_text('\n'.join(lines))
    all_c_files.append(func_table_path)

    # Generate Ninja manifest
    root_dir = Path(__file__).resolve().parent.parent
    ninja_path = out_dir / "build.ninja"
    generate_ninja_manifest(ninja_path, all_c_files, lib_out, root_dir)

    # Execute Ninja build with strict job throttling to prevent memory/swap exhaustion
    print(f"\n[Builder] Compiling {len(all_c_files)} chunks with Ninja (jobs={jobs})...")
    cmd = ["ninja", "-j", str(jobs), "-f", str(ninja_path)]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("[Builder Error] Ninja compilation failed!")
        sys.exit(res.returncode)

    elapsed = time.time() - t0
    lib_size = lib_out.stat().st_size if lib_out.exists() else 0
    print(f"\n=====================================================")
    print(f"  Successfully compiled {lib_out.name} ({lib_size:,} bytes) in {elapsed:.1f}s")
    print(f"  Registered Functions: {len(all_fn_addrs):,}")
    print(f"=====================================================")


def main():
    parser = argparse.ArgumentParser(description="TotK-Recomp Game Library Builder")
    parser.add_argument('--exefs-dir', type=str, default="local/exefs", help="Directory containing extracted ExeFS NSOs")
    parser.add_argument('--out-dir', type=str, default="work/gen", help="Output directory for C source files")
    parser.add_argument('--lib-out', type=str, default="build/libtotk_game.so", help="Output shared library path")
    parser.add_argument('--max-main-fns', type=int, default=1000, help="Max functions from main to lift")
    parser.add_argument('--jobs', '-j', type=int, default=2, help="Number of concurrent compilation jobs (default: 2)")
    args = parser.parse_args()

    build_game_library(Path(args.exefs_dir), Path(args.out_dir), Path(args.lib_out), max_main_fns=args.max_main_fns, jobs=args.jobs)


if __name__ == '__main__':
    main()
