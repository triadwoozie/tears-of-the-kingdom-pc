"""Relocate whole LL/SC regions, including alternate entries sharing a store."""

LOADS = {'ldxr', 'ldaxr', 'ldxp', 'ldaxp', 'ldxrb', 'ldaxrb', 'ldxrh', 'ldaxrh'}
STORES = {'stxr', 'stlxr', 'stxp', 'stlxp', 'stxrb', 'stlxrb', 'stxrh', 'stlxrh'}


def conditional(ins):
    return ins.mnemonic in ('cbz', 'cbnz', 'tbz', 'tbnz') or ins.mnemonic.startswith('b.')


def successors(ins):
    if ins.mnemonic in ('ret', 'br', 'blr', 'bl', 'clrex'):
        return []
    if ins.mnemonic == 'b':
        return [ins.operands[-1].imm]
    if conditional(ins):
        return [ins.operands[-1].imm, ins.address + 4]
    return [ins.address + 4]


def find_windows(insl, has18):
    instructions = {x.address: x for x in insl}
    parents = {}
    for x in insl:
        if x.mnemonic in STORES:
            continue
        for dest in successors(x):
            if dest in instructions and instructions[dest].mnemonic not in LOADS:
                parents.setdefault(dest, set()).add(x.address)
    # Walk backwards from each store, stopping at the reservation's load.
    live = set()
    pending = [x.address for x in insl if x.mnemonic in STORES]
    while pending:
        addr = pending.pop()
        if addr in live:
            continue
        live.add(addr)
        if instructions[addr].mnemonic not in LOADS:
            pending.extend(parents.get(addr, ()))
    regions = []
    for x in insl:
        if x.mnemonic not in LOADS or x.address not in live:
            continue
        nodes, pending = set(), [x.address]
        while pending:
            addr = pending.pop()
            if addr in nodes:
                continue
            nodes.add(addr)
            ins = instructions[addr]
            if ins.mnemonic not in STORES:
                pending.extend(y for y in successors(ins) if y in live and
                               instructions[y].mnemonic not in LOADS)
        overlaps = [r for r in regions if r & nodes]
        for r in overlaps:
            nodes |= r
            regions.remove(r)
        regions.append(nodes)
    regions = [r for r in regions if any(has18(instructions[a]) for a in r)]
    for r in regions:
        for x in insl:
            for dest in successors(x):
                if dest in r and x.address not in r:
                    assert instructions[dest].mnemonic in LOADS, f'entry without exclusive load: {dest:x}'
    return [[instructions[a] for a in sorted(r)] for r in regions]


def site_window(seg, env):
    used = set().union(*(env['regs_in'](x) for x in seg))
    scratch, temp = env['pick'](used)
    emit, here, branch = env['emit'], env['here'], env['B']
    entries = {}
    for x in seg:
        if x.mnemonic in LOADS:
            entries[x.address] = here()
            emit(env['STP'](scratch, temp, 31, -16))
            emit(env['MRS_TPIDRRO'](temp))
            env['emit_slot_addr'](temp, scratch, 'K18')
            emit(env['LDRX'](scratch, temp))
            emit(0)  # Filled once the relocated instruction addresses are known.
    entry_ends = [entries[a] for a in entries][1:] + [here()]
    positions = {}
    for x in seg:
        positions[x.address] = here()
        emit(0)
        if x.mnemonic != 'b':
            emit(0)
    exits = {}
    for x in seg:
        targets = [x.address + 4] if x.mnemonic in STORES else successors(x)
        for dest in targets:
            # A new exclusive load starts a fresh reservation and restores scratch first.
            if (dest not in positions or dest in entries) and dest not in exits:
                exits[dest] = here()
                emit(env['STRX'](scratch, temp))
                emit(env['LDP'](scratch, temp, 31, -16))
                emit(branch(here(), dest))
    import struct

    def put(pc, word):
        struct.pack_into('<I', env['patch'], pc - env['PATCH_OFF'], word)

    for addr, end in zip(entries, entry_ends):
        put(end - 4, branch(end - 4, positions[addr]))
    for x in seg:
        pc = positions[x.address]
        word = env['subst'](x, scratch) if env['has18'](x) else int.from_bytes(x.bytes, 'little')

        def target(addr):
            return exits[addr] if addr in exits else positions[addr]

        if x.mnemonic == 'b':
            put(pc, branch(pc, target(x.operands[-1].imm)))
        elif conditional(x):
            dest = target(x.operands[-1].imm)
            delta = dest - pc
            bits = 14 if x.mnemonic in ('tbz', 'tbnz') else 19
            assert delta % 4 == 0 and -(1 << (bits + 1)) <= delta < (1 << (bits + 1))
            mask = ((1 << bits) - 1) << 5
            word = (word & ~mask) | (((delta >> 2) << 5) & mask)
            assert env['dis'](word, pc).operands[-1].imm == dest
            put(pc, word)
            put(pc + 4, branch(pc + 4, target(x.address + 4)))
        else:
            assert x.mnemonic not in ('adr', 'adrp', 'bl', 'blr', 'br', 'ret')
            assert not (x.mnemonic == 'ldr' and '[' not in x.op_str)
            assert env['sp_delta'](x) == 0
            put(pc, word)
            put(pc + 4, branch(pc + 4, target(x.address + 4)))
    return entries
