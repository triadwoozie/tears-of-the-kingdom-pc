#include "module_loader.h"
#include "memory_manager.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace totk::runtime {

// Self-contained LZ4 block decompressor
static bool lz4_decompress_block(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len) {
    size_t ip = 0;
    size_t op = 0;
    while (ip < src_len && op < dst_len) {
        uint8_t token = src[ip++];
        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t s;
            do {
                if (ip >= src_len) return false;
                s = src[ip++];
                lit_len += s;
            } while (s == 255);
        }
        if (ip + lit_len > src_len) return false;
        size_t lit_copy = std::min(lit_len, dst_len - op);
        std::memcpy(dst + op, src + ip, lit_copy);
        ip += lit_len;
        op += lit_copy;
        if (ip >= src_len || op >= dst_len) break;

        if (ip + 2 > src_len) return false;
        uint16_t offset = static_cast<uint16_t>(src[ip] | (src[ip + 1] << 8));
        ip += 2;
        if (offset == 0 || offset > op) return false;

        size_t match_len = (token & 0x0F) + 4;
        if (match_len == 19) {
            uint8_t s;
            do {
                if (ip >= src_len) return false;
                s = src[ip++];
                match_len += s;
            } while (s == 255);
        }
        size_t match_copy = std::min(match_len, dst_len - op);
        size_t match_src = op - offset;
        for (size_t i = 0; i < match_copy; ++i) {
            dst[op++] = dst[match_src++];
        }
    }
    return op == dst_len;
}

// Self-contained standard SHA-256 implementation
static void sha256_hash(const uint8_t *data, size_t len, uint8_t hash[32]) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t padded_len = ((len + 8 + 64) / 64) * 64;
    std::vector<uint8_t> msg(padded_len, 0);
    std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i) {
        msg[padded_len - 1 - i] = static_cast<uint8_t>(bits >> (i * 8));
    }

    auto rotr = [](uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); };

    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h_val + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h_val = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
    }

    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
}

ModuleLoader::ModuleLoader() = default;

ModuleLoader::~ModuleLoader() {
    unload();
}

ModuleLoader &ModuleLoader::instance() {
    static ModuleLoader s_loader;
    return s_loader;
}

bool ModuleLoader::load(const std::string &path) {
    unload();
    m_module_path = path;
    m_last_error.clear();

#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) {
        DWORD err = GetLastError();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "LoadLibraryA failed with error code %lu", err);
        m_last_error = buf;
        return false;
    }
    m_handle = reinterpret_cast<void *>(h);
#else
    void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        const char *err = dlerror();
        m_last_error = err ? err : "Unknown dlopen error";
        return false;
    }
    m_handle = h;
#endif

    const char *common_hooks[] = {
        "register_recompiled_functions",
        "register_main_functions",
        "register_subsdk0_functions",
        "register_sdk_functions"
    };

    for (const char *hook : common_hooks) {
        void *fn = resolve_symbol(hook);
        if (fn) {
            auto reg = reinterpret_cast<ModuleRegisterFn>(fn);
            reg();
            m_registered_hooks.push_back(hook);
        }
    }

    return true;
}

bool ModuleLoader::load_nso(const std::string &path, uint64_t base_addr, const std::string &module_name) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        m_last_error = "Could not open NSO file: " + path;
        return false;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char *>(buffer.data()), file_size)) {
        m_last_error = "Failed to read NSO file: " + path;
        return false;
    }

    std::string mod_name = module_name;
    if (mod_name.empty()) {
        size_t last_slash = path.find_last_of("/\\");
        mod_name = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : path;
        size_t dot = mod_name.find_last_of('.');
        if (dot != std::string::npos) mod_name = mod_name.substr(0, dot);
    }

    return parse_and_map_nso(buffer.data(), buffer.size(), base_addr, mod_name, path);
}

bool ModuleLoader::load_nso_memory(const uint8_t *data, size_t size, uint64_t base_addr, const std::string &module_name) {
    return parse_and_map_nso(data, size, base_addr, module_name, "<memory>");
}

bool ModuleLoader::parse_and_map_nso(const uint8_t *raw, size_t raw_size, uint64_t base_addr,
                                     const std::string &module_name, const std::string &path) {
    if (raw_size < sizeof(NsoHeader)) {
        m_last_error = "File size smaller than NSO header";
        return false;
    }

    NsoHeader header;
    std::memcpy(&header, raw, sizeof(NsoHeader));

    if (header.magic != 0x304F534E) { // 'NSO0'
        m_last_error = "Invalid NSO magic header";
        return false;
    }

    std::vector<std::vector<uint8_t>> decompressed_segs(3);
    for (int i = 0; i < 3; ++i) {
        uint32_t file_off = header.segments[i].file_offset;
        uint32_t comp_sz = header.compressed_sizes[i];
        uint32_t dec_sz = header.segments[i].size;

        if (file_off + comp_sz > raw_size) {
            m_last_error = "Segment exceeds NSO file boundary";
            return false;
        }

        decompressed_segs[i].resize(dec_sz);
        if (header.flags & (1 << i)) {
            if (!lz4_decompress_block(raw + file_off, comp_sz, decompressed_segs[i].data(), dec_sz)) {
                m_last_error = "LZ4 decompression failed for segment " + std::to_string(i);
                return false;
            }
        } else {
            std::memcpy(decompressed_segs[i].data(), raw + file_off, std::min<size_t>(comp_sz, dec_sz));
        }

        // SHA-256 hash verification
        if (header.flags & (1 << (i + 3))) {
            uint8_t hash[32];
            sha256_hash(decompressed_segs[i].data(), dec_sz, hash);
            if (std::memcmp(hash, header.hashes[i], 32) != 0) {
                m_last_error = "SHA-256 verification failed for segment " + std::to_string(i);
                return false;
            }
        }
    }

    LoadedModule mod;
    mod.name = module_name;
    mod.path = path;
    mod.base_addr = base_addr;
    mod.text_off = header.segments[0].memory_offset;
    mod.text_size = header.segments[0].size;
    mod.rodata_off = header.segments[1].memory_offset;
    mod.rodata_size = header.segments[1].size;
    mod.data_off = header.segments[2].memory_offset;
    mod.data_size = header.segments[2].size;
    mod.bss_size = header.segments[2].align_or_reserved;

    char build_hex[65] = {0};
    for (int i = 0; i < 32; ++i) {
        std::snprintf(build_hex + i * 2, 3, "%02x", header.build_id[i]);
    }
    mod.build_id = build_hex;

    size_t total_size = mod.data_off + mod.data_size + mod.bss_size;
    total_size = (total_size + 0xFFF) & ~0xFFF;
    mod.size = total_size;

    // Ensure memory range is committed in 39-bit memory manager
    auto &mm = MemoryManager::instance();
    mm.commit(base_addr, total_size);

    uint8_t *guest_dest = reinterpret_cast<uint8_t *>(base_addr);
    for (int i = 0; i < 3; ++i) {
        std::memcpy(guest_dest + header.segments[i].memory_offset,
                    decompressed_segs[i].data(),
                    decompressed_segs[i].size());
    }
    // Zero BSS
    if (mod.bss_size > 0) {
        std::memset(guest_dest + mod.data_off + mod.data_size, 0, mod.bss_size);
    }

    // Parse MOD0 dynamic structure
    if (total_size >= 8) {
        uint32_t mod0_off = *reinterpret_cast<const uint32_t *>(guest_dest + 4);
        if (mod0_off + 4 <= total_size && std::memcmp(guest_dest + mod0_off, "MOD0", 4) == 0) {
            auto get_rel = [&](int idx) -> uint64_t {
                int32_t rel_val = *reinterpret_cast<const int32_t *>(guest_dest + mod0_off + 4 + idx * 4);
                return mod0_off + rel_val;
            };
            uint64_t dyn_off = get_rel(0);

            // Read Dynamic Table
            std::unordered_map<uint64_t, uint64_t> dyn;
            uint64_t cur_dyn = dyn_off;
            while (cur_dyn + 16 <= total_size) {
                uint64_t tag = *reinterpret_cast<const uint64_t *>(guest_dest + cur_dyn);
                uint64_t val = *reinterpret_cast<const uint64_t *>(guest_dest + cur_dyn + 8);
                cur_dyn += 16;
                if (tag == 0) break; // DT_NULL
                dyn[tag] = val;
            }

            uint64_t strtab = dyn[5]; // DT_STRTAB
            uint64_t symtab = dyn[6]; // DT_SYMTAB
            uint64_t hash_off = dyn[4]; // DT_HASH

            // Parse Symbols
            if (strtab != 0 && symtab != 0 && hash_off != 0 && hash_off + 8 <= total_size) {
                uint32_t nchain = *reinterpret_cast<const uint32_t *>(guest_dest + hash_off + 4);
                for (uint32_t i = 0; i < nchain; ++i) {
                    uint64_t sym_offset = symtab + i * 24;
                    if (sym_offset + 24 > total_size) break;

                    uint32_t st_name = *reinterpret_cast<const uint32_t *>(guest_dest + sym_offset);
                    uint16_t st_shndx = *reinterpret_cast<const uint16_t *>(guest_dest + sym_offset + 6);
                    uint64_t st_value = *reinterpret_cast<const uint64_t *>(guest_dest + sym_offset + 8);

                    if (strtab + st_name < total_size) {
                        const char *sym_name = reinterpret_cast<const char *>(guest_dest + strtab + st_name);
                        if (sym_name[0] != '\0') {
                            if (st_shndx != 0) {
                                uint64_t abs_addr = base_addr + st_value;
                                mod.exports[sym_name] = abs_addr;
                                m_global_symbols[sym_name] = abs_addr;
                            } else {
                                mod.imports.push_back(sym_name);
                            }
                        }
                    }
                }
            }

            // Parse RELA relocations
            auto parse_rela = [&](uint64_t base_tag, uint64_t size_tag) {
                if (dyn.count(base_tag) && dyn.count(size_tag)) {
                    uint64_t r_base = dyn[base_tag];
                    uint64_t r_count = dyn[size_tag] / 24;
                    for (uint64_t i = 0; i < r_count; ++i) {
                        uint64_t o = r_base + i * 24;
                        if (o + 24 > total_size) break;
                        uint64_t r_offset = *reinterpret_cast<const uint64_t *>(guest_dest + o);
                        uint64_t r_info = *reinterpret_cast<const uint64_t *>(guest_dest + o + 8);
                        int64_t r_addend = *reinterpret_cast<const int64_t *>(guest_dest + o + 16);

                        ElfRelocation r{};
                        r.offset = r_offset;
                        r.type = static_cast<uint32_t>(r_info & 0xFFFFFFFF);
                        r.sym_idx = static_cast<uint32_t>(r_info >> 32);
                        r.addend = r_addend;
                        mod.relocations.push_back(r);
                    }
                }
            };

            parse_rela(7, 8);   // DT_RELA, DT_RELASZ
            parse_rela(23, 2);  // DT_JMPREL, DT_PLTRELSZ

            // Parse DT_RELR (36: table, 35: size)
            if (dyn.count(36) && dyn.count(35)) {
                uint64_t relr_base = dyn[36];
                uint64_t relr_size = dyn[35];
                uint64_t relr_count = relr_size / 8;
                for (uint64_t i = 0; i < relr_count; ++i) {
                    if (relr_base + (i + 1) * 8 <= total_size) {
                        uint64_t val = *reinterpret_cast<const uint64_t *>(guest_dest + relr_base + i * 8);
                        mod.relr_entries.push_back(val);
                    }
                }
            }
        }
    }

    std::printf("[ModuleLoader] Loaded NSO '%s' at 0x%016llx (size: %zu KB, exports: %zu, relocs: %zu)\n",
                mod.name.c_str(), static_cast<unsigned long long>(base_addr), total_size / 1024,
                mod.exports.size(), mod.relocations.size() + mod.relr_entries.size());

    m_loaded_modules.push_back(std::move(mod));
    return true;
}

size_t ModuleLoader::apply_relocations() {
    size_t applied_count = 0;

    for (auto &mod : m_loaded_modules) {
        // Apply standard RELA relocations
        for (const auto &r : mod.relocations) {
            uint64_t target_addr = mod.base_addr + r.offset;
            uint64_t *target_slot = reinterpret_cast<uint64_t *>(target_addr);

            switch (r.type) {
                case 1027: // R_AARCH64_RELATIVE
                    *target_slot = mod.base_addr + r.addend;
                    applied_count++;
                    break;
                case 1025: // R_AARCH64_GLOB_DAT
                case 1026: // R_AARCH64_JUMP_SLOT
                case 257:  // R_AARCH64_ABS64
                    // Resolve via sym_idx in dynsym
                    *target_slot = mod.base_addr + r.addend;
                    applied_count++;
                    break;
                default:
                    break;
            }
        }

        // Apply DT_RELR compact relative relocations
        uint64_t where = 0;
        for (uint64_t entry : mod.relr_entries) {
            if ((entry & 1) == 0) {
                where = mod.base_addr + entry;
                *reinterpret_cast<uint64_t *>(where) += mod.base_addr;
                where += 8;
                applied_count++;
            } else {
                for (int bit = 1; bit < 64; ++bit) {
                    if ((entry >> bit) & 1) {
                        uint64_t slot = where + (bit - 1) * 8;
                        *reinterpret_cast<uint64_t *>(slot) += mod.base_addr;
                        applied_count++;
                    }
                }
                where += 63 * 8;
            }
        }
    }

    std::printf("[ModuleLoader] Applied %zu dynamic relocations across %zu modules\n",
                applied_count, m_loaded_modules.size());
    return applied_count;
}

uint64_t ModuleLoader::resolve_guest_symbol(const std::string &symbol_name) const {
    auto it = m_global_symbols.find(symbol_name);
    return (it != m_global_symbols.end()) ? it->second : 0;
}

void ModuleLoader::unload() {
    if (m_handle) {
#if defined(_WIN32)
        FreeLibrary(reinterpret_cast<HMODULE>(m_handle));
#else
        dlclose(m_handle);
#endif
        m_handle = nullptr;
    }
    m_module_path.clear();
    m_registered_hooks.clear();
    m_loaded_modules.clear();
    m_global_symbols.clear();
}

void *ModuleLoader::resolve_symbol(const std::string &symbol_name) {
    if (!m_handle) {
        m_last_error = "No module loaded";
        return nullptr;
    }

#if defined(_WIN32)
    void *sym = reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(m_handle), symbol_name.c_str()));
    if (!sym) {
        DWORD err = GetLastError();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "GetProcAddress('%s') failed with error %lu", symbol_name.c_str(), err);
        m_last_error = buf;
    }
    return sym;
#else
    dlerror();
    void *sym = dlsym(m_handle, symbol_name.c_str());
    const char *err = dlerror();
    if (err) {
        m_last_error = err;
        return nullptr;
    }
    return sym;
#endif
}

bool ModuleLoader::call_registration_hook(const std::string &hook_name) {
    void *sym = resolve_symbol(hook_name);
    if (!sym) {
        return false;
    }

    auto hook_fn = reinterpret_cast<ModuleRegisterFn>(sym);
    hook_fn();
    m_registered_hooks.push_back(hook_name);
    return true;
}

size_t ModuleLoader::register_symbol_table(const ModuleFunctionSymbol *symbols, size_t count) {
    if (!symbols || count == 0) return 0;

    size_t registered = 0;
    for (size_t i = 0; i < count; ++i) {
        if (symbols[i].fn) {
            totk::recompiler::register_recompiled_func(symbols[i].guest_addr, symbols[i].fn);
            if (symbols[i].name) {
                m_global_symbols[symbols[i].name] = symbols[i].guest_addr;
            }
            registered++;
        }
    }
    return registered;
}

bool ModuleLoader::execute_entrypoint(const std::string &entry_name, totk::recompiler::Arm64Context *ctx) {
    if (!ctx) {
        m_last_error = "Arm64Context is null";
        return false;
    }

    // Check host dynamic symbol
    void *sym = resolve_symbol(entry_name);
    if (sym) {
        auto entry_fn = reinterpret_cast<ModuleEntryFn>(sym);
        entry_fn(ctx);
        return true;
    }

    // Check guest recompiled symbol
    uint64_t guest_addr = resolve_guest_symbol(entry_name);
    if (guest_addr != 0) {
        totk::recompiler::guest_indirect_call(ctx, guest_addr);
        return true;
    }

    m_last_error = "Symbol not found: " + entry_name;
    return false;
}

} // namespace totk::runtime
