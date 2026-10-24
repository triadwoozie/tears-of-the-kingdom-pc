#pragma once

#include "recompiler/arm64_ctx.h"
#include "recompiler/dispatch.h"
#include <cstdint>
#include <string>
#include <vector>

namespace totk::runtime {

struct ModuleFunctionSymbol {
    uint64_t guest_addr;
    totk::recompiler::RecompiledFunc fn;
    const char *name;
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

    // Unload the currently loaded module
    void unload();

    // Check if a module is currently loaded
    bool is_loaded() const { return m_handle != nullptr; }

    // Resolve an exported symbol by name
    void *resolve_symbol(const std::string &symbol_name);

    // Call standard registration functions (e.g. register_recompiled_functions)
    bool call_registration_hook(const std::string &hook_name = "register_recompiled_functions");

    // Register a batch of symbol mappings directly into the dispatch table
    size_t register_symbol_table(const ModuleFunctionSymbol *symbols, size_t count);

    // Execute the entrypoint function
    bool execute_entrypoint(const std::string &entry_name, totk::recompiler::Arm64Context *ctx);

    // Get the last error string
    const std::string &get_last_error() const { return m_last_error; }

    // Get current module path
    const std::string &get_module_path() const { return m_module_path; }

    // Singleton instance access
    static ModuleLoader &instance();

private:
    void *m_handle{nullptr};
    std::string m_module_path;
    std::string m_last_error;
    std::vector<std::string> m_registered_hooks;
};

} // namespace totk::runtime
