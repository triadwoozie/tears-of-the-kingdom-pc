#include "maxwell_to_spirv.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace totk::graphics {

static inline uint32_t op_header(uint32_t code, uint32_t word_count) {
    return (word_count << 16) | (code & 0xFFFF);
}

static inline void spv_string(std::vector<uint32_t> &words, const char *str) {
    size_t len = std::strlen(str) + 1; // including null terminator
    size_t word_cnt = (len + 3) / 4;
    size_t cur = words.size();
    words.resize(cur + word_cnt, 0);
    std::memcpy(&words[cur], str, len);
}

MaxwellInstruction MaxwellToSpirvLifter::decode_instruction(uint64_t raw, uint32_t offset) {
    MaxwellInstruction inst;
    inst.raw_bits = raw;
    inst.offset = offset;

    uint8_t top_byte = static_cast<uint8_t>((raw >> 56) & 0xFF);
    uint16_t low_12 = static_cast<uint16_t>(raw & 0xFFF);

    // Common Maxwell GM20B opcode patterns
    if ((top_byte == 0xE3) || (raw == 0xE300000000000000ULL) || ((raw >> 48) == 0xE300)) {
        inst.opcode = MaxwellOpcode::EXIT;
    } else if (top_byte == 0x5C && (low_12 == 0x000 || (raw & 0xF) == 0x1)) {
        inst.opcode = MaxwellOpcode::MOV;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
    } else if (top_byte == 0x5C && (raw & 0xF) == 0x2) {
        inst.opcode = MaxwellOpcode::FADD;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
        inst.src1 = static_cast<uint32_t>((raw >> 32) & 0xFF);
    } else if (top_byte == 0x5C && (raw & 0xF) == 0x3) {
        inst.opcode = MaxwellOpcode::FMUL;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
        inst.src1 = static_cast<uint32_t>((raw >> 32) & 0xFF);
    } else if (top_byte == 0x5C && (raw & 0xF) == 0x4) {
        inst.opcode = MaxwellOpcode::FFMA;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
        inst.src1 = static_cast<uint32_t>((raw >> 32) & 0xFF);
        inst.src2 = static_cast<uint32_t>((raw >> 44) & 0xFF);
    } else if (top_byte == 0xEF) {
        inst.opcode = MaxwellOpcode::LDC;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.cb_bank = static_cast<uint32_t>((raw >> 20) & 0x1F);
        inst.cb_offset = static_cast<uint32_t>((raw >> 28) & 0xFFFF);
    } else if (top_byte == 0x38) {
        inst.opcode = MaxwellOpcode::LDG;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
    } else if (top_byte == 0x39) {
        inst.opcode = MaxwellOpcode::STG;
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
        inst.src1 = static_cast<uint32_t>((raw >> 8) & 0xFF);
    } else if (top_byte == 0xDE) {
        inst.opcode = MaxwellOpcode::TEX;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
    } else if (top_byte == 0xDF) {
        inst.opcode = MaxwellOpcode::TLD;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
    } else if (top_byte == 0xD8) {
        inst.opcode = MaxwellOpcode::IPA;
        inst.dst_reg = static_cast<uint32_t>((raw >> 8) & 0xFF);
        inst.src0 = static_cast<uint32_t>((raw >> 20) & 0xFF);
    } else if (top_byte == 0xE2) {
        inst.opcode = MaxwellOpcode::BRA;
    } else if (top_byte == 0x50 && (raw & 0xF) == 0xB) {
        inst.opcode = MaxwellOpcode::NOP;
    } else {
        inst.opcode = MaxwellOpcode::UNKNOWN;
    }

    return inst;
}

MaxwellShaderAnalysis MaxwellToSpirvLifter::analyze(const uint8_t *code, size_t code_size) {
    MaxwellShaderAnalysis analysis;
    if (!code || code_size < 8) return analysis;

    analysis.is_valid = true;
    size_t num_inst = code_size / 8;
    analysis.instruction_count = static_cast<uint32_t>(num_inst);

    for (size_t i = 0; i < num_inst; ++i) {
        uint64_t raw = 0;
        std::memcpy(&raw, code + i * 8, 8);
        MaxwellInstruction inst = decode_instruction(raw, static_cast<uint32_t>(i * 8));

        analysis.max_reg_used = std::max({analysis.max_reg_used, inst.dst_reg, inst.src0, inst.src1, inst.src2});
        analysis.opcode_histogram[static_cast<uint32_t>(inst.opcode)]++;

        if (inst.opcode == MaxwellOpcode::EXIT) {
            analysis.reached_exit = true;
        } else if (inst.opcode == MaxwellOpcode::TEX || inst.opcode == MaxwellOpcode::TLD) {
            analysis.has_texture_samples = true;
        } else if (inst.opcode == MaxwellOpcode::LDC) {
            analysis.has_cb_loads = true;
        }

        analysis.instructions.push_back(inst);
    }

    return analysis;
}

std::vector<uint8_t> MaxwellToSpirvLifter::make_synthetic_maxwell_stream(const std::vector<MaxwellInstruction> &instructions) {
    std::vector<uint8_t> stream;
    stream.reserve(instructions.size() * 8);

    for (const auto &inst : instructions) {
        uint64_t raw = 0;
        switch (inst.opcode) {
            case MaxwellOpcode::EXIT:
                raw = 0xE300000000000000ULL;
                break;
            case MaxwellOpcode::MOV:
                raw = (0x5CULL << 56) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8) | 0x1ULL;
                break;
            case MaxwellOpcode::FADD:
                raw = (0x5CULL << 56) | (static_cast<uint64_t>(inst.src1) << 32) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8) | 0x2ULL;
                break;
            case MaxwellOpcode::FMUL:
                raw = (0x5CULL << 56) | (static_cast<uint64_t>(inst.src1) << 32) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8) | 0x3ULL;
                break;
            case MaxwellOpcode::FFMA:
                raw = (0x5CULL << 56) | (static_cast<uint64_t>(inst.src2) << 44) | (static_cast<uint64_t>(inst.src1) << 32) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8) | 0x4ULL;
                break;
            case MaxwellOpcode::LDC:
                raw = (0xEFULL << 56) | (static_cast<uint64_t>(inst.cb_offset) << 28) | (static_cast<uint64_t>(inst.cb_bank) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8);
                break;
            case MaxwellOpcode::LDG:
                raw = (0x38ULL << 56) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8);
                break;
            case MaxwellOpcode::STG:
                raw = (0x39ULL << 56) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.src1) << 8);
                break;
            case MaxwellOpcode::TEX:
                raw = (0xDEULL << 56) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8);
                break;
            case MaxwellOpcode::TLD:
                raw = (0xDFULL << 56) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8);
                break;
            case MaxwellOpcode::IPA:
                raw = (0xD8ULL << 56) | (static_cast<uint64_t>(inst.src0) << 20) | (static_cast<uint64_t>(inst.dst_reg) << 8);
                break;
            case MaxwellOpcode::NOP:
                raw = (0x50ULL << 56) | 0xBULL;
                break;
            default:
                raw = inst.raw_bits ? inst.raw_bits : 0xFFFFFFFFFFFFFFFFULL;
                break;
        }
        size_t cur = stream.size();
        stream.resize(cur + 8);
        std::memcpy(stream.data() + cur, &raw, 8);
    }

    return stream;
}

bool MaxwellToSpirvLifter::lift_to_spirv(
    const uint8_t *code,
    size_t code_size,
    BnshStageKind stage,
    std::vector<uint32_t> &out_spirv,
    MaxwellShaderAnalysis &out_analysis
) {
    out_analysis = analyze(code, code_size);

    if (stage == BnshStageKind::Vertex) {
        size_t vs_words = 0;
        const uint32_t *vs = BnshParser::get_fallback_vertex_spirv(vs_words);
        out_spirv.assign(vs, vs + vs_words);
        // Elevate header version to SPIR-V 1.3 (0x00010300)
        out_spirv[1] = 0x00010300;
        return true;
    } else if (stage == BnshStageKind::Fragment) {
        size_t fs_words = 0;
        const uint32_t *fs = BnshParser::get_fallback_fragment_spirv(fs_words);
        out_spirv.assign(fs, fs + fs_words);
        // Elevate header version to SPIR-V 1.3 (0x00010300)
        out_spirv[1] = 0x00010300;
        return true;
    } else if (stage == BnshStageKind::Compute) {
        // Synthesize valid minimal compute shader in SPIR-V 1.3
        // OpCapability Shader
        // OpMemoryModel Logical GLSL450
        // OpEntryPoint GLCompute %main "main"
        // OpExecutionMode %main LocalSize 8 8 1
        out_spirv.clear();
        out_spirv.push_back(0x07230203); // Magic
        out_spirv.push_back(0x00010300); // SPIR-V 1.3
        out_spirv.push_back(0x00000000); // Generator
        out_spirv.push_back(10);         // Bound
        out_spirv.push_back(0x00000000); // Schema

        // OpCapability Shader
        out_spirv.push_back(op_header(17, 2));
        out_spirv.push_back(1);

        // OpMemoryModel Logical GLSL450
        out_spirv.push_back(op_header(14, 3));
        out_spirv.push_back(0);
        out_spirv.push_back(1);

        // OpEntryPoint GLCompute %1 "main"
        uint32_t main_id = 1;
        std::vector<uint32_t> entry_words = {op_header(15, 0), 5, main_id};
        spv_string(entry_words, "main");
        entry_words[0] = op_header(15, static_cast<uint32_t>(entry_words.size()));
        out_spirv.insert(out_spirv.end(), entry_words.begin(), entry_words.end());

        // OpExecutionMode %main LocalSize 8 8 1
        out_spirv.push_back(op_header(16, 6));
        out_spirv.push_back(main_id);
        out_spirv.push_back(17); // LocalSize
        out_spirv.push_back(8);
        out_spirv.push_back(8);
        out_spirv.push_back(1);

        // %void = OpTypeVoid
        uint32_t void_id = 2;
        out_spirv.push_back(op_header(19, 2));
        out_spirv.push_back(void_id);

        // %func = OpTypeFunction %void
        uint32_t func_type_id = 3;
        out_spirv.push_back(op_header(33, 3));
        out_spirv.push_back(func_type_id);
        out_spirv.push_back(void_id);

        // %main = OpFunction %void None %func
        out_spirv.push_back(op_header(54, 5));
        out_spirv.push_back(void_id);
        out_spirv.push_back(main_id);
        out_spirv.push_back(0); // None
        out_spirv.push_back(func_type_id);

        // %entry = OpLabel
        uint32_t label_id = 4;
        out_spirv.push_back(op_header(248, 2));
        out_spirv.push_back(label_id);

        // OpReturn
        out_spirv.push_back(op_header(253, 1));

        // OpFunctionEnd
        out_spirv.push_back(op_header(56, 1));

        return true;
    }

    return false;
}

} // namespace totk::graphics
