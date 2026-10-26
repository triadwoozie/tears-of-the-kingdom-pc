/*
 * PC-2 crash-trace index.
 *
 * Lifted function bodies are intentionally not copied into this bridge handoff.
 * Consult the local refs/ultratears checkout only if authorized to access the
 * generated game code; the paths and identifiers below narrow that lookup.
 *
 * f_800cb5e1c: data/generated/main/main_554.c
 * f_80006a920: data/generated/main/main_031.c
 * f_802bb74c0: data/generated/main/main_1182.c
 * f_805143c60: data/generated/sdk/sdk_020.c
 * f_800c85f50: data/generated/main/main_547.c
 * f_80092de78: data/generated/main/main_461.c
 *
 * Reported crash: f_800cb5e1c calls f_80006a920 (sead::Heap::alloc, 0x230
 * bytes); the heap pointer in c->x[1] is 0, associated with f_802bb74c0 and
 * TLS slot f_805143c60. Allocation returns c->x[0] = 0, then the caller faults
 * while writing at c->x[0] + 0x8.
 */
