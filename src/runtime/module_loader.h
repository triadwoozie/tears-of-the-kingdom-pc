#pragma once

#include "recompiler/arm64_ctx.h"
#include "recompiler/dispatch.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace totk::runtime {

#pragma pack(push, 1)
struct NsoSegmentHeader {
    uint32_t file_offset;
    uint32_t memory_offset;
    uint32_t size;
    uint32_t align_or_reserved;
};

struct NsoHeader {
    uint32_t magic; // "NSO0"
    uint32_t version;
    uint32_t reserved;
    uint32_t flags;
    NsoSegmentHeader segments[3]; // 0: text, 1: rodata, 2: data (0x10 to 0x40)
    uint8_t build_id[0x20];       // 0x40 to 0x60
    uint32_t compressed_sizes[3]; // 0x60 to 0x6C
    uint8_t padding[0x34];        // 0x6C to 0xA0
    uint8_t hashes[3][0x20];      // 0xA0 to 0x100
};
#pragma pack(pop)

struct ModuleFunctionSymbol {
    uint64_t guest_addr;
    totk::recompiler::RecompiledFunc fn;
    const char *name;
};

struct ElfRelocation {
    uint64_t offset;
    uint32_t type;
    uint32_t sym_idx;
    int64_t addend;
    std::string sym_name;
};

struct LoadedModule {
    std::string name;
    std::string path;
    uint64_t base_addr = 0;
    size_t size = 0;
    uint32_t text_off = 0;
    uint32_t text_size = 0;
    uint32_t rodata_off = 0;
    uint32_t rodata_size = 0;
    uint32_t data_off = 0;
    uint32_t data_size = 0;
    uint32_t bss_size = 0;
    std::string build_id;
    std::unordered_map<std::string, uint64_t> exports;
    std::vector<std::string> imports;
    std::vector<ElfRelocation> relocations;
    std::vector<uint64_t> relr_entries;
};

using ModuleRegisterFn = void (*)();
using ModuleEntryFn = void (*)(totk::recompiler::Arm64Context *ctx);

class ModuleLoader {
public:
    ModuleLoader();
    ~ModuleLoader();

    // Non-copyable
    ModuleLoader(const ModuleLoader &) = delete;
    ModuleLoader &operator=(const ModuleLoader &) = delete;

    // Load a dynamic library (.so on Linux, .dll on Windows)
    bool load(const std::string &path);

    // Load and decompress a raw Nintendo NSO0 binary into the 39-bit guest window
    bool load_nso(const std::string &path, uint64_t base_addr, const std::string &module_name = "");

    // Load NSO directly from memory buffer
    bool load_nso_memory(const uint8_t *data, size_t size, uint64_t base_addr, const std::string &module_name = "");

    // Apply ELF dynamic relocations across all loaded NSO modules
    size_t apply_relocations();

    // Resolve an exported guest symbol across loaded modules
    uint64_t resolve_guest_symbol(const std::string &symbol_name) const;

    // Unload the currently loaded module(s)
    void unload();

    // Check if any module is loaded
    bool is_loaded() const { return m_handle != nullptr || !m_loaded_modules.empty(); }

    // Resolve an exported host symbol by name (for dynamic libraries)
    void *resolve_symbol(const std::string &symbol_name);

    // Call standard registration functions
    bool call_registration_hook(const std::string &hook_name = "register_recompiled_functions");

    // Register a batch of symbol mappings directly into the dispatch table
    size_t register_symbol_table(const ModuleFunctionSymbol *symbols, size_t count);

    // Execute the entrypoint function
    bool execute_entrypoint(const std::string &entry_name, totk::recompiler::Arm64Context *ctx);

    // Get loaded NSO modules
    const std::vector<LoadedModule> &get_loaded_modules() const { return m_loaded_modules; }

    // Get the last error string
    const std::string &get_last_error() const { return m_last_error; }

    // Get current module path
    const std::string &get_module_path() const { return m_module_path; }

    // Singleton instance access
    static ModuleLoader &instance();

private:
    bool parse_and_map_nso(const uint8_t *raw, size_t raw_size, uint64_t base_addr, const std::string &module_name, const std::string &path);

    void *m_handle{nullptr};
    std::string m_module_path;
    std::string m_last_error;
    std::vector<std::string> m_registered_hooks;
    std::vector<LoadedModule> m_loaded_modules;
    std::unordered_map<std::string, uint64_t> m_global_symbols;
};

} // namespace totk::runtime
