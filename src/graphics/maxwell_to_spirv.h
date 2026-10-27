#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "graphics/bnsh_parser.h"

namespace totk::graphics {

enum class MaxwellOpcode : uint32_t {
    NOP = 0x0,
    MOV = 0x1,
    FADD = 0x2,
    FMUL = 0x3,
    FFMA = 0x4,
    LDC = 0x5,
    LDG = 0x6,
    STG = 0x7,
    TEX = 0x8,
    TLD = 0x9,
    IPA = 0xA,
    EXIT = 0xB,
    BRA = 0xC,
    UNKNOWN = 0xFF
};

struct MaxwellInstruction {
    uint64_t raw_bits = 0;
    uint32_t offset = 0;
    MaxwellOpcode opcode = MaxwellOpcode::UNKNOWN;
    uint32_t dst_reg = 0;
    uint32_t src0 = 0;
    uint32_t src1 = 0;
    uint32_t src2 = 0;
    uint32_t cb_bank = 0;
    uint32_t cb_offset = 0;
    float imm_f32 = 0.0f;
    bool has_imm = false;
};

struct MaxwellShaderAnalysis {
    bool is_valid = false;
    uint32_t instruction_count = 0;
    uint32_t max_reg_used = 0;
    bool reached_exit = false;
    bool has_texture_samples = false;
    bool has_cb_loads = false;
    std::unordered_map<uint32_t, uint32_t> opcode_histogram;
    std::vector<MaxwellInstruction> instructions;
};

class MaxwellToSpirvLifter {
public:
    static MaxwellInstruction decode_instruction(uint64_t raw, uint32_t offset = 0);
    static MaxwellShaderAnalysis analyze(const uint8_t *code, size_t code_size);

    // Lifts Maxwell instructions to valid SPIR-V 1.3 module.
    // If unsupported complex control flow occurs, falls back gracefully to passthrough SPIR-V while tracking analysis.
    static bool lift_to_spirv(
        const uint8_t *code,
        size_t code_size,
        BnshStageKind stage,
        std::vector<uint32_t> &out_spirv,
        MaxwellShaderAnalysis &out_analysis
    );

    // Helper: generate synthetic Maxwell instruction stream for testing
    static std::vector<uint8_t> make_synthetic_maxwell_stream(const std::vector<MaxwellInstruction> &instructions);
};

} // namespace totk::graphics
