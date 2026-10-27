#include "graphics/maxwell_to_spirv.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace totk::graphics;

static void test_instruction_decoding_and_analysis() {
    std::cout << "[TEST] Maxwell Instruction Decoding & Analysis..." << std::endl;

    std::vector<MaxwellInstruction> instructions = {
        // MOV R1, R0
        {0, 0, MaxwellOpcode::MOV, 1, 0, 0, 0, 0, 0, 0.0f, false},
        // FADD R2, R1, R0
        {0, 0, MaxwellOpcode::FADD, 2, 1, 0, 0, 0, 0, 0.0f, false},
        // FMUL R3, R2, R1
        {0, 0, MaxwellOpcode::FMUL, 3, 2, 1, 0, 0, 0, 0.0f, false},
        // FFMA R4, R3, R2, R1
        {0, 0, MaxwellOpcode::FFMA, 4, 3, 2, 1, 0, 0, 0.0f, false},
        // LDC R5, c[0x2][0x140]
        {0, 0, MaxwellOpcode::LDC, 5, 0, 0, 0, 2, 0x140, 0.0f, false},
        // LDG R6, [R5]
        {0, 0, MaxwellOpcode::LDG, 6, 5, 0, 0, 0, 0, 0.0f, false},
        // STG [R6], R4
        {0, 0, MaxwellOpcode::STG, 0, 6, 4, 0, 0, 0, 0.0f, false},
        // TEX R7, R6
        {0, 0, MaxwellOpcode::TEX, 7, 6, 0, 0, 0, 0, 0.0f, false},
        // TLD R8, R7
        {0, 0, MaxwellOpcode::TLD, 8, 7, 0, 0, 0, 0, 0.0f, false},
        // IPA R9, R8
        {0, 0, MaxwellOpcode::IPA, 9, 8, 0, 0, 0, 0, 0.0f, false},
        // NOP
        {0, 0, MaxwellOpcode::NOP, 0, 0, 0, 0, 0, 0, 0.0f, false},
        // EXIT
        {0, 0, MaxwellOpcode::EXIT, 0, 0, 0, 0, 0, 0, 0.0f, false}
    };

    auto bytecode = MaxwellToSpirvLifter::make_synthetic_maxwell_stream(instructions);
    assert(bytecode.size() == instructions.size() * 8);

    MaxwellShaderAnalysis analysis = MaxwellToSpirvLifter::analyze(bytecode.data(), bytecode.size());
    assert(analysis.is_valid);
    assert(analysis.instruction_count == 12);
    assert(analysis.reached_exit);
    assert(analysis.has_texture_samples);
    assert(analysis.has_cb_loads);
    assert(analysis.max_reg_used >= 9);

    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::MOV)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::FADD)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::FMUL)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::FFMA)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::LDC)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::LDG)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::STG)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::TEX)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::TLD)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::IPA)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::NOP)] == 1);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::EXIT)] == 1);

    std::cout << "  Passed decoding and analysis checks." << std::endl;
}

static void test_spirv_lifting_stages() {
    std::cout << "[TEST] Maxwell to SPIR-V 1.3 Lifting for All Stages..." << std::endl;

    std::vector<MaxwellInstruction> instructions = {
        {0, 0, MaxwellOpcode::LDC, 1, 0, 0, 0, 0, 0x10, 0.0f, false},
        {0, 0, MaxwellOpcode::TEX, 2, 1, 0, 0, 0, 0, 0.0f, false},
        {0, 0, MaxwellOpcode::EXIT, 0, 0, 0, 0, 0, 0, 0.0f, false}
    };
    auto bytecode = MaxwellToSpirvLifter::make_synthetic_maxwell_stream(instructions);

    // 1. Vertex Stage
    {
        std::vector<uint32_t> spirv;
        MaxwellShaderAnalysis analysis;
        bool ok = MaxwellToSpirvLifter::lift_to_spirv(bytecode.data(), bytecode.size(), BnshStageKind::Vertex, spirv, analysis);
        assert(ok);
        assert(spirv.size() > 5);
        assert(spirv[0] == 0x07230203); // SPIR-V Magic
        assert(spirv[1] == 0x00010300); // SPIR-V 1.3
        assert(analysis.reached_exit);
        assert(analysis.has_texture_samples);
    }

    // 2. Fragment Stage
    {
        std::vector<uint32_t> spirv;
        MaxwellShaderAnalysis analysis;
        bool ok = MaxwellToSpirvLifter::lift_to_spirv(bytecode.data(), bytecode.size(), BnshStageKind::Fragment, spirv, analysis);
        assert(ok);
        assert(spirv.size() > 5);
        assert(spirv[0] == 0x07230203);
        assert(spirv[1] == 0x00010300);
        assert(analysis.reached_exit);
    }

    // 3. Compute Stage
    {
        std::vector<uint32_t> spirv;
        MaxwellShaderAnalysis analysis;
        bool ok = MaxwellToSpirvLifter::lift_to_spirv(bytecode.data(), bytecode.size(), BnshStageKind::Compute, spirv, analysis);
        assert(ok);
        assert(spirv.size() > 5);
        assert(spirv[0] == 0x07230203);
        assert(spirv[1] == 0x00010300);
        assert(analysis.reached_exit);
    }

    std::cout << "  Passed SPIR-V 1.3 stage lifting checks." << std::endl;
}

static void test_fallback_on_unsupported_opcodes() {
    std::cout << "[TEST] Maxwell Graceful Fallback on Unknown Opcodes..." << std::endl;

    std::vector<MaxwellInstruction> instructions = {
        // Unknown raw bytecode
        {0xCAFEBABE12345678ULL, 0, MaxwellOpcode::UNKNOWN, 0, 0, 0, 0, 0, 0, 0.0f, false},
        {0, 0, MaxwellOpcode::EXIT, 0, 0, 0, 0, 0, 0, 0.0f, false}
    };
    auto bytecode = MaxwellToSpirvLifter::make_synthetic_maxwell_stream(instructions);

    std::vector<uint32_t> spirv;
    MaxwellShaderAnalysis analysis;
    bool ok = MaxwellToSpirvLifter::lift_to_spirv(bytecode.data(), bytecode.size(), BnshStageKind::Fragment, spirv, analysis);
    assert(ok);
    assert(analysis.opcode_histogram[static_cast<uint32_t>(MaxwellOpcode::UNKNOWN)] == 1);
    assert(spirv.size() > 5);
    assert(spirv[0] == 0x07230203);

    std::cout << "  Passed fallback on unsupported opcodes check." << std::endl;
}

int main() {
    std::cout << "=== Running Maxwell to SPIR-V Lifter Tests ===" << std::endl;
    test_instruction_decoding_and_analysis();
    test_spirv_lifting_stages();
    test_fallback_on_unsupported_opcodes();
    std::cout << "=== All Maxwell to SPIR-V Lifter Tests Passed! ===" << std::endl;
    return 0;
}
