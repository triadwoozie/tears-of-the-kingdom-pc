#include "module_loader.h"
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace totk::runtime {

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

    // Attempt default auto-registration hooks if present in the module
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
    dlerror(); // Clear existing errors
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

    void *sym = resolve_symbol(entry_name);
    if (!sym) {
        return false;
    }

    auto entry_fn = reinterpret_cast<ModuleEntryFn>(sym);
    entry_fn(ctx);
    return true;
}

} // namespace totk::runtime
