#!/usr/bin/env python3
"""Inspect selected lifted functions and callsites without dumping large chunks."""

from __future__ import annotations

import argparse
import re
import subprocess
from collections import deque
from pathlib import Path
from io import StringIO


ROOTS = (0x7104AF51A4, 0x7104BFE1B4, 0x7104BE5830)
FUNCTION_RE = re.compile(r"^\s*void\s+f_([0-9a-fA-F]+)\s*\(")
CALL_RE = re.compile(r"\bf_(710[0-9a-fA-F]{7,13})\s*\(")
RETURN_RE = re.compile(r"c->x\[30\]\s*=\s*0x([0-9a-fA-F]+)ull")
PC_RE = re.compile(r"c->pc\s*=\s*0x([0-9a-fA-F]+)ull")
DISPATCH_RE = re.compile(
    r"c->pc\s*=\s*0x([0-9a-fA-F]+)ull\s*;\s*rt_dispatch\s*\(\s*c\s*\)"
)
HOOK_RE = re.compile(r"register_recompiled_func\s*\(\s*0x([0-9a-fA-F]+)")


def load_symbols(path: Path) -> dict[int, str]:
    symbols: dict[int, str] = {}
    if path.is_file():
        source = path.open(encoding="utf-8", errors="replace")
    else:
        try:
            source = StringIO(subprocess.run(
                ["git", "show", "origin/symbol-table:work/symbols.tsv"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout)
            print(f"Using locally fetched symbol table from origin/symbol-table (missing {path}).")
        except (OSError, subprocess.CalledProcessError):
            print(f"WARNING: symbol table not found: {path}")
            return symbols
    with source:
        for line in source:
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 2:
                continue
            try:
                name = fields[2] if len(fields) >= 3 else fields[1]
                symbols[int(fields[0], 0)] = name
            except ValueError:
                continue
    return symbols


def dispatched_target(context: list[str], return_address: int) -> int:
    for index, line in enumerate(context):
        match = RETURN_RE.search(line)
        if not match or int(match.group(1), 16) != return_address:
            continue
        for candidate in context[index:index + 4]:
            pc_match = PC_RE.search(candidate)
            if pc_match and "rt_dispatch" in candidate:
                return int(pc_match.group(1), 16)
        for candidate in context[index:index + 4]:
            pc_match = PC_RE.search(candidate)
            if pc_match:
                return int(pc_match.group(1), 16)
    return 0


def scan(root: Path, symbols_path: Path, runtime_dir: Path) -> int:
    files = sorted(root.glob("main_chunk_*.c")) + sorted(root.glob("sdk_chunk_*.c"))
    if not files:
        print(f"ERROR: no lifted main/sdk chunks in {root}")
        return 2

    symbols = load_symbols(symbols_path)
    hooks: set[int] = set()
    for path in list(runtime_dir.glob("*.cpp")) + list(runtime_dir.glob("*.h")):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            print(f"WARNING: cannot inspect hooks in {path}: {error}")
            continue
        hooks.update(int(value, 16) for value in HOOK_RE.findall(text))

    roots = set(ROOTS)
    function_slices: dict[int, tuple[Path, int, list[str]]] = {}
    dispatched_targets: set[int] = set()
    return_sites: dict[int, list[tuple[Path, int, int, int, list[str]]]] = {
        address: [] for address in ROOTS
    }

    for path in files:
        try:
            with path.open(encoding="utf-8", errors="replace") as source:
                pending: tuple[int, int, list[str], int] | None = None
                window: deque[tuple[int, str]] = deque(maxlen=25)
                active_sites: list[tuple[int, int, int, list[str]]] = []
                for line_number, line in enumerate(source, 1):
                    rendered = line.rstrip()
                    for site in active_sites:
                        site[3].append(rendered)
                    active_sites = [site for site in active_sites if len(site[3]) < 25]

                    match = FUNCTION_RE.match(line)
                    if match:
                        if pending:
                            address, start, body, _ = pending
                            if address in roots and address not in function_slices:
                                function_slices[address] = (path, start, body)
                        address = int(match.group(1), 16)
                        pending = (address, line_number, [], 0)
                    if pending:
                        address, start, body, depth = pending
                        if len(body) < 50:
                            body.append(rendered)
                        depth += line.count("{") - line.count("}")
                        pending = (address, start, body, depth)
                        if depth <= 0:
                            if address in roots and address not in function_slices:
                                function_slices[address] = (path, start, body)
                            pending = None

                    ret = RETURN_RE.search(line)
                    if ret:
                        return_address = int(ret.group(1), 16)
                        if return_address in return_sites:
                            context = [text for _, text in window] + [rendered]
                            site = (line_number, return_address, 0, context)
                            active_sites.append(site)
                            if len(context) >= 25:
                                target = dispatched_target(context, return_address)
                                return_sites[return_address].append(
                                    (path, line_number, pending[0] if pending else 0,
                                     target, context[-25:])
                                )
                                active_sites.remove(site)
                    window.append((line_number, rendered))
                    for site in active_sites:
                        if len(site[3]) >= 25:
                            _, return_address, _, context = site
                            target = dispatched_target(context, return_address)
                            return_sites[return_address].append(
                                (path, site[0], pending[0] if pending else 0,
                                 target, context[-25:])
                            )
                    active_sites = [site for site in active_sites if len(site[3]) < 25]
        except OSError as error:
            print(f"WARNING: cannot read {path}: {error}")

    print(f"Indexed {len(files)} generated C files; symbols: {len(symbols)}; "
          f"registered hooks: {len(hooks)}.")
    callees: set[int] = set()
    for address in ROOTS:
        name = symbols.get(address, "unknown symbol")
        print(f"\n=== 0x{address:x} {name} ===")
        found = function_slices.get(address)
        if found:
            path, start, body = found
            print(f"Definition: {path}:{start}")
            callees_for_function = {
                int(value, 16) for line in body[1:] for value in CALL_RE.findall(line)
            }
            callees.update(callees_for_function)
            for offset, line in enumerate(body):
                print(f"{start + offset}: {line}")
            print("Direct lifted callees:")
            for callee in sorted(callees_for_function):
                suffix = f" {symbols[callee]}" if callee in symbols else ""
                hook_status = "hooked" if callee in hooks else "not hooked"
                print(f"  0x{callee:x}{suffix} ({hook_status})")
        else:
            print("No function definition at this address (may be a return address).")

        sites = return_sites[address]
        for path, line_number, caller, target, context in sites:
            caller_name = symbols.get(caller, "unknown caller") if caller else "unknown caller"
            target_name = symbols.get(target, "unknown symbol") if target else "unresolved"
            print(f"Return site in 0x{caller:x} {caller_name}: {path}:{line_number}; "
                  f"dispatched target "
                  f"0x{target:x} {target_name}")
            if target:
                dispatched_targets.add(target)
            for dispatch_target in {
                int(value, 16)
                for line in context
                for value in DISPATCH_RE.findall(line)
            }:
                dispatched_targets.add(dispatch_target)
                dispatch_name = symbols.get(dispatch_target, "unresolved")
                print(f"  nearby dispatch: 0x{dispatch_target:x} {dispatch_name} "
                      f"({'hooked' if dispatch_target in hooks else 'not hooked'})")
            for line in context:
                print(f"  {line}")

    print("\n=== Cross-reference ===")
    for callee in sorted(callees | dispatched_targets):
        print(f"0x{callee:x}\t{symbols.get(callee, 'unresolved')}\t"
              f"{'hooked' if callee in hooks else 'not hooked'}")
    print("No guest code or hooks were changed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", type=Path, default=Path("work/gen"))
    parser.add_argument("--symbols", type=Path, default=Path("work/symbols.tsv"))
    parser.add_argument("--runtime-dir", type=Path, default=Path("src/runtime"))
    args = parser.parse_args()
    return scan(args.generated_dir, args.symbols, args.runtime_dir)


if __name__ == "__main__":
    raise SystemExit(main())
