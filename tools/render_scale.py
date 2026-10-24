"""Version-bound internal render scale; address verified against TotK 1.4.3 (Build ID in game_timing.py).

main+0xd5c24 computes each view's render size: screen size x the view's scale (1920x1080 x 5/6 for the scene) x the
dynamic-resolution factor, clamped to 8..16384 and floored to the view's alignment (1600x896). Factor 1.0 preserves
the original selection. Other factors replace the dynamic multiplier with 1, then scale the view's base size.
"""
import hashlib
import struct

from game_timing import BUILD_ID, STATE_OFFSET

SCALE_OFFSET = STATE_OFFSET + 32   # float, after the timing fields (+8 movie, +12 ceiling, +16 seconds, +24 lead)
SITE = 0xd5cc0
ORIGINAL = [0x52a8d008,   # mov  w8, #0x46800000 (16384.0, the upper clamp)
            0x1e250840,   # fmul s0, s2, s5      width x view scale
            0x1e240861,   # fmul s1, s3, s4      height x view scale
            0x1e270104]   # fmov s4, w8
RESUME = SITE + 16
DYNAMIC_SITE = 0xd5d80
DYNAMIC_ORIGINAL = 0xbd402906   # ldr s6, [x8, #0x28]: the selected dynamic-resolution multiplier


def apply_render_scale(env):
    if env['BUILD_ID'] != BUILD_ID:
        raise ValueError('render scale requires the verified v1.4.3 main Build ID')
    image, emit, here = env['IMG'], env['emit'], env['here']
    if SCALE_OFFSET + 4 > env['PATCH_OFF'] or image[STATE_OFFSET:STATE_OFFSET + 8] != b'ZFPS143\0':
        raise ValueError('render scale needs the timing state page')
    if any(image[SCALE_OFFSET:SCALE_OFFSET + 4]):
        raise ValueError('render scale field overlaps timing state')
    if list(struct.unpack_from('<4I', image, SITE)) != ORIGINAL:
        raise ValueError(f'render scale instruction mismatch at {SITE:#x}')
    if struct.unpack_from('<I', image, DYNAMIC_SITE)[0] != DYNAMIC_ORIGINAL:
        raise ValueError(f'dynamic scale instruction mismatch at {DYNAMIC_SITE:#x}')
    struct.pack_into('<f', image, SCALE_OFFSET, 1.0)
    start = here()
    emit(env['ADRP'](8, here(), STATE_OFFSET))
    for word in (
        0xbd400106 | ((SCALE_OFFSET & 0xfff) // 4) << 10,   # ldr  s6, [x8, #scale]  (s6 is dead until fmov s6, #8.0)
        ORIGINAL[1], ORIGINAL[2],                           # original view-scale multiplies
        0x1e260800, 0x1e260821,                             # fmul s0, s0, s6; fmul s1, s1, s6
        ORIGINAL[0], ORIGINAL[3],                           # w8 = 16384.0; s4 = w8
    ):
        emit(word)
    emit(env['B'](here(), RESUME))
    body = bytes(env['patch'][start - env['PATCH_OFF']:])
    old = bytes(image[SITE:SITE + 4])
    new = struct.pack('<I', env['B'](SITE, start))
    image[SITE:SITE + 4] = new
    writes = [dict(offset=SITE, original=old.hex(), replacement=new.hex(), reason='render size x host factor')]
    routines = {'render_scale': dict(offset=start, size=len(body), sha256=hashlib.sha256(body).hexdigest())}
    start = here()
    emit(0xa9bf47f0)   # stp x16, x17, [sp, #-16]!
    emit(env['ADRP'](16, here(), STATE_OFFSET))
    emit(0xb9400210 | ((SCALE_OFFSET & 0xfff) // 4) << 10)   # ldr w16, [x16, #scale]
    emit(0x52a7f011)   # mov w17, #0x3f800000 (1.0f)
    emit(0x4a110210)   # eor w16, w16, w17; leaves NZCV untouched
    emit(0x35000070)   # cbnz w16, fixed (+12)
    emit(DYNAMIC_ORIGINAL)
    emit(env['B'](here(), here() + 8))
    emit(0x1e2e1006)   # fixed: fmov s6, #1.0
    emit(0xa8c147f0)   # ldp x16, x17, [sp], #16
    emit(env['B'](here(), DYNAMIC_SITE + 4))
    body = bytes(env['patch'][start - env['PATCH_OFF']:])
    new = struct.pack('<I', env['B'](DYNAMIC_SITE, start))
    writes.append(dict(offset=DYNAMIC_SITE, original=struct.pack('<I', DYNAMIC_ORIGINAL).hex(),
                       replacement=new.hex(), reason='preserve dynamic scale only in original mode'))
    image[DYNAMIC_SITE:DYNAMIC_SITE + 4] = new
    routines['dynamic_scale'] = dict(offset=start, size=len(body), sha256=hashlib.sha256(body).hexdigest())
    return dict(build_id=BUILD_ID, scale_offset=SCALE_OFFSET, default=1.0, writes=writes, routines=routines)
