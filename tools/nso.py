"""NSO loader: decompress segments, build the memory image, parse MOD0/dynamic info."""
import hashlib
import struct
import sys

try:
    import lz4.block
    def lz4_decompress(data, uncompressed_size):
        return lz4.block.decompress(data, uncompressed_size=uncompressed_size)
except ImportError:
    def lz4_decompress(data, uncompressed_size):
        src = memoryview(data)
        dst = bytearray()
        ip = 0
        src_len = len(src)
        while ip < src_len:
            token = src[ip]
            ip += 1
            lit_len = token >> 4
            if lit_len == 15:
                while ip < src_len:
                    s = src[ip]
                    ip += 1
                    lit_len += s
                    if s != 255: break
            dst.extend(src[ip:ip+lit_len])
            ip += lit_len
            if ip >= src_len: break
            offset = src[ip] | (src[ip+1] << 8)
            ip += 2
            match_len = (token & 0x0F) + 4
            if match_len == 19:
                while ip < src_len:
                    s = src[ip]
                    ip += 1
                    match_len += s
                    if s != 255: break
            pos = len(dst) - offset
            for _ in range(match_len):
                dst.append(dst[pos])
                pos += 1
        return bytes(dst[:uncompressed_size])


DT_NULL, DT_NEEDED, DT_PLTRELSZ, DT_HASH, DT_STRTAB, DT_SYMTAB, DT_RELA, DT_RELASZ = 0, 1, 2, 4, 5, 6, 7, 8
DT_STRSZ, DT_JMPREL, DT_RELACOUNT = 10, 23, 0x6FFFFFF9
R_AARCH64_ABS64, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT, R_AARCH64_RELATIVE = 257, 1025, 1026, 1027


class Nso:
    def __init__(self, path):
        if isinstance(path, (bytes, bytearray)):
            raw = bytes(path)
        else:
            raw = open(path, 'rb').read()
        assert raw[:4] == b'NSO0', f"Invalid NSO magic: {path}"
        self.flags = struct.unpack_from('<I', raw, 0xC)[0]
        self.build_id = raw[0x40:0x60].rstrip(b'\0').hex()
        segs = []
        seg_hashes = [raw[0xA0:0xC0], raw[0xC0:0xE0], raw[0xE0:0x100]]
        self.hashes_verified = True

        for i in range(3):
            file_off, mem_off, dec_size = struct.unpack_from('<III', raw, 0x10 + i * 0x10)
            comp_size = struct.unpack_from('<I', raw, 0x60 + i * 4)[0]
            data = raw[file_off:file_off + comp_size]
            if self.flags & (1 << i):
                data = lz4_decompress(data, dec_size)

            # SHA-256 hash check if flag bit is set
            if self.flags & (1 << (i + 3)):
                actual_hash = hashlib.sha256(data).digest()
                if actual_hash != seg_hashes[i]:
                    self.hashes_verified = False

            segs.append((mem_off, data))

        self.bss_size = struct.unpack_from('<I', raw, 0x3C)[0]
        self.text = (segs[0][0], len(segs[0][1]))
        self.rodata = (segs[1][0], len(segs[1][1]))
        self.data = (segs[2][0], len(segs[2][1]))
        end = segs[2][0] + len(segs[2][1]) + self.bss_size
        self.size = (end + 0xFFF) & ~0xFFF
        img = bytearray(self.size)
        for off, d in segs:
            img[off:off + len(d)] = d
        self.image = img
        self._parse_mod0()

    def u32(self, off): return struct.unpack_from('<I', self.image, off)[0]
    def u64(self, off): return struct.unpack_from('<Q', self.image, off)[0]

    def _parse_mod0(self):
        mod0 = self.u32(4)
        if mod0 + 4 > len(self.image) or self.image[mod0:mod0 + 4] != b'MOD0':
            self.dynamic = 0
            self.syms = []
            self.relocs = []
            self.needed = []
            return
        rel = lambda i: mod0 + struct.unpack_from('<i', self.image, mod0 + 4 + i * 4)[0]
        self.dynamic, self.bss_start, self.bss_end = rel(0), rel(1), rel(2)
        self.eh_start, self.eh_end, self.module_obj = rel(3), rel(4), rel(5)
        dyn = {}
        needed = []
        off = self.dynamic
        while off + 16 <= len(self.image):
            tag, val = self.u64(off), self.u64(off + 8)
            off += 16
            if tag == DT_NULL:
                break
            if tag == DT_NEEDED:
                needed.append(val)
            dyn.setdefault(tag, val)
        self.dyn = dyn
        strtab = dyn.get(DT_STRTAB, 0)
        self.strtab = strtab
        self.needed = [self.cstr(strtab + n) for n in needed if strtab + n < len(self.image)]
        # dynsym count from DT_HASH or (DT_STRTAB - DT_SYMTAB) // 24
        self.syms = []
        if DT_SYMTAB in dyn:
            st = dyn[DT_SYMTAB]
            strtab = dyn.get(DT_STRTAB, 0)
            if DT_HASH in dyn:
                nchain = self.u32(dyn[DT_HASH] + 4)
            elif strtab > st:
                nchain = (strtab - st) // 24
            else:
                nchain = 0
            for i in range(nchain):
                o = st + i * 24
                if o + 24 > len(self.image): break
                name, info, other, shndx = struct.unpack_from('<IBBH', self.image, o)
                value, size = struct.unpack_from('<QQ', self.image, o + 8)
                self.syms.append((self.cstr(strtab + name), info, shndx, value, size))
        self.relocs = []
        for base_tag, size_tag in ((DT_RELA, DT_RELASZ), (DT_JMPREL, DT_PLTRELSZ)):
            if base_tag in dyn:
                b, n = dyn[base_tag], dyn[size_tag] // 24
                for i in range(n):
                    o = b + i * 24
                    if o + 24 > len(self.image): break
                    r_off, r_info, r_add = struct.unpack_from('<QQq', self.image, o)
                    self.relocs.append((r_off, r_info & 0xFFFFFFFF, r_info >> 32, r_add))

        DT_RELR = 36
        DT_RELRSZ = 35
        if DT_RELR in dyn and DT_RELRSZ in dyn:
            relr_off = dyn[DT_RELR]
            relr_sz = dyn[DT_RELRSZ]
            words = struct.unpack_from(f"<{relr_sz // 8}Q", self.image, relr_off)
            cur_off = 0
            for w in words:
                if (w & 1) == 0:
                    cur_off = w
                    if cur_off + 8 <= len(self.image):
                        addend = struct.unpack_from("<Q", self.image, cur_off)[0]
                        self.relocs.append((cur_off, R_AARCH64_RELATIVE, 0, addend))
                    cur_off += 8
                else:
                    bitmap = w >> 1
                    bit = 0
                    while bitmap:
                        if bitmap & 1:
                            off = cur_off + bit * 8
                            if off + 8 <= len(self.image):
                                addend = struct.unpack_from("<Q", self.image, off)[0]
                                self.relocs.append((off, R_AARCH64_RELATIVE, 0, addend))
                        bitmap >>= 1
                        bit += 1
                    cur_off += 63 * 8

    def cstr(self, off):
        if off >= len(self.image): return ""
        try:
            end = self.image.index(b'\0', off)
            return self.image[off:end].decode('utf-8', 'replace')
        except ValueError:
            return ""

    def imports(self):
        return [s for s in self.syms if s[0] and s[2] == 0]

    def exports(self):
        return [s for s in self.syms if s[0] and s[2] != 0]


if __name__ == '__main__':
    from collections import Counter
    for p in sys.argv[1:]:
        n = Nso(p)
        print(f'== {p}  build {n.build_id[:16]}  image 0x{n.size:x}')
        print(f'  text 0x{n.text[0]:x}+0x{n.text[1]:x}  ro 0x{n.rodata[0]:x}+0x{n.rodata[1]:x}  '
              f'data 0x{n.data[0]:x}+0x{n.data[1]:x}  bss 0x{n.bss_size:x}')
        print(f'  needed {n.needed}  syms {len(n.syms)}  imports {len(n.imports())}  exports {len(n.exports())}')
        print(f'  relocs {dict(Counter(r[1] for r in n.relocs))}')
