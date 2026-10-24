#include "runtime/module_loader.h"
#include "recompiler/arm64_ctx.h"
#include "recompiler/dispatch.h"
#include <cassert>
#include <cstdio>

static bool s_mock_called_1 = false;
static bool s_mock_called_2 = false;

static void mock_recompiled_fn_1(totk::recompiler::Arm64Context *ctx) {
    s_mock_called_1 = true;
    ctx->x[0] = 0x12345678ULL;
}

static void mock_recompiled_fn_2(totk::recompiler::Arm64Context *ctx) {
    s_mock_called_2 = true;
    ctx->x[1] = ctx->x[0] + 42;
}

void test_module_loader_errors() {
    std::printf("[Test] Running test_module_loader_errors...\n");

    auto &loader = totk::runtime::ModuleLoader::instance();
    assert(!loader.is_loaded());

    // Loading a non-existent shared object must return false and record error
    bool loaded = loader.load("non_existent_game_module_99999.so");
    assert(!loaded);
    assert(!loader.is_loaded());
    assert(!loader.get_last_error().empty());

    // Resolving symbol on unloaded module returns nullptr
    void *sym = loader.resolve_symbol("nnMain");
    assert(sym == nullptr);

    // Entrypoint execution with null context returns false
    bool exec = loader.execute_entrypoint("nnMain", nullptr);
    assert(!exec);

    std::printf("  -> Module Loader Error Handling: PASSED\n");
}

void test_module_loader_symbol_registration() {
    std::printf("[Test] Running test_module_loader_symbol_registration...\n");

    auto &loader = totk::runtime::ModuleLoader::instance();

    totk::runtime::ModuleFunctionSymbol symbols[] = {
        {0x7100010000ULL, mock_recompiled_fn_1, "f_main_10000"},
        {0x7100010020ULL, mock_recompiled_fn_2, "f_main_10020"}
    };

    size_t count = loader.register_symbol_table(symbols, 2);
    assert(count == 2);

    totk::recompiler::Arm64Context ctx{};

    // Invoke first registered guest function via radix dispatch
    totk::recompiler::guest_indirect_call(&ctx, 0x7100010000ULL);
    assert(s_mock_called_1 == true);
    assert(ctx.x[0] == 0x12345678ULL);

    // Invoke second registered guest function
    totk::recompiler::guest_indirect_call(&ctx, 0x7100010020ULL);
    assert(s_mock_called_2 == true);
    assert(ctx.x[1] == 0x12345678ULL + 42);

    std::printf("  -> Module Loader Symbol Table & Indirect Call: PASSED\n");
}

int main() {
    std::printf("==================================================\n");
    std::printf("  Dynamic Module Loader Unit Tests\n");
    std::printf("==================================================\n");

    test_module_loader_errors();
    test_module_loader_symbol_registration();

    std::printf("\nAll Module Loader unit tests PASSED successfully!\n");
    return 0;
}
