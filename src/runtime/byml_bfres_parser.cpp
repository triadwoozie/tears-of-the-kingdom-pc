#include "byml_bfres_parser.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace totk::runtime {

static inline uint32_t read_u24_le(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

static inline uint32_t read_u24_be(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

static inline uint32_t read_u32_le(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static inline uint32_t read_u32_be(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static bool parse_string_table(const uint8_t *base, size_t size, uint32_t offset, bool is_be,
                               std::vector<std::string> &out_strings) {
    if (offset == 0 || offset + 4 > size) return true;
    const uint8_t *p = base + offset;
    if (p[0] != static_cast<uint8_t>(BymlType::StringTable)) return false;

    uint32_t count = is_be ? read_u24_be(p + 1) : read_u24_le(p + 1);
    size_t table_header_size = 4 + (count + 1) * 4;
    if (offset + table_header_size > size) return false;

    out_strings.clear();
    out_strings.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t str_off = is_be ? read_u32_be(p + 4 + i * 4) : read_u32_le(p + 4 + i * 4);
        if (offset + str_off >= size) {
            out_strings.push_back("");
            continue;
        }
        const char *str_ptr = reinterpret_cast<const char *>(base + offset + str_off);
        out_strings.push_back(std::string(str_ptr));
    }
    return true;
}

static bool parse_node(const uint8_t *base, size_t size, uint32_t node_offset, BymlType type, bool is_be,
                       const std::vector<std::string> &keys, const std::vector<std::string> &strings,
                       BymlValue &out_val);

static bool parse_node(const uint8_t *base, size_t size, uint32_t node_offset, BymlType type, bool is_be,
                       const std::vector<std::string> &keys, const std::vector<std::string> &strings,
                       BymlValue &out_val) {
    if (node_offset >= size) return false;
    const uint8_t *p = base + node_offset;

    switch (type) {
        case BymlType::Dictionary: {
            if (p[0] != static_cast<uint8_t>(BymlType::Dictionary)) return false;
            uint32_t entry_count = is_be ? read_u24_be(p + 1) : read_u24_le(p + 1);
            if (node_offset + 4 + entry_count * 8 > size) return false;

            out_val = BymlValue::make_dict();
            const uint8_t *entry_ptr = p + 4;
            for (uint32_t i = 0; i < entry_count; ++i, entry_ptr += 8) {
                uint32_t key_idx = is_be ? read_u24_be(entry_ptr) : read_u24_le(entry_ptr);
                BymlType val_type = static_cast<BymlType>(entry_ptr[3]);
                uint32_t raw_val = is_be ? read_u32_be(entry_ptr + 4) : read_u32_le(entry_ptr + 4);

                std::string key = (key_idx < keys.size()) ? keys[key_idx] : ("key_" + std::to_string(key_idx));
                BymlValue child;

                if (val_type == BymlType::Dictionary || val_type == BymlType::Array) {
                    if (!parse_node(base, size, raw_val, val_type, is_be, keys, strings, child)) {
                        return false;
                    }
                } else if (val_type == BymlType::String) {
                    child = (raw_val < strings.size()) ? BymlValue(strings[raw_val]) : BymlValue("");
                } else if (val_type == BymlType::Bool) {
                    child = BymlValue(raw_val != 0);
                } else if (val_type == BymlType::Int32) {
                    child = BymlValue(static_cast<int32_t>(raw_val));
                } else if (val_type == BymlType::UInt32) {
                    child = BymlValue(raw_val);
                } else if (val_type == BymlType::Float32) {
                    float f;
                    std::memcpy(&f, &raw_val, 4);
                    child = BymlValue(f);
                } else {
                    child = BymlValue(static_cast<int32_t>(raw_val));
                }

                out_val.set(key, child);
            }
            return true;
        }

        case BymlType::Array: {
            if (p[0] != static_cast<uint8_t>(BymlType::Array)) return false;
            uint32_t elem_count = is_be ? read_u24_be(p + 1) : read_u24_le(p + 1);
            size_t type_array_size = (elem_count + 3) & ~3ULL;
            if (node_offset + 4 + type_array_size + elem_count * 4 > size) return false;

            out_val = BymlValue::make_array();
            const uint8_t *types_ptr = p + 4;
            const uint8_t *vals_ptr = p + 4 + type_array_size;

            for (uint32_t i = 0; i < elem_count; ++i) {
                BymlType val_type = static_cast<BymlType>(types_ptr[i]);
                uint32_t raw_val = is_be ? read_u32_be(vals_ptr + i * 4) : read_u32_le(vals_ptr + i * 4);
                BymlValue child;

                if (val_type == BymlType::Dictionary || val_type == BymlType::Array) {
                    if (!parse_node(base, size, raw_val, val_type, is_be, keys, strings, child)) {
                        return false;
                    }
                } else if (val_type == BymlType::String) {
                    child = (raw_val < strings.size()) ? BymlValue(strings[raw_val]) : BymlValue("");
                } else if (val_type == BymlType::Bool) {
                    child = BymlValue(raw_val != 0);
                } else if (val_type == BymlType::Int32) {
                    child = BymlValue(static_cast<int32_t>(raw_val));
                } else if (val_type == BymlType::UInt32) {
                    child = BymlValue(raw_val);
                } else if (val_type == BymlType::Float32) {
                    float f;
                    std::memcpy(&f, &raw_val, 4);
                    child = BymlValue(f);
                } else {
                    child = BymlValue(static_cast<int32_t>(raw_val));
                }

                out_val.push(child);
            }
            return true;
        }

        default:
            return false;
    }
}

bool BymlParser::parse(const uint8_t *data, size_t size, BymlValue &out_root) {
    if (!data || size < 16) return false;

    bool is_le = (data[0] == 'Y' && data[1] == 'B');
    bool is_be = (data[0] == 'B' && data[1] == 'Y');
    if (!is_le && !is_be) return false;

    uint32_t key_table_offset = is_be ? read_u32_be(data + 4) : read_u32_le(data + 4);
    uint32_t str_table_offset = is_be ? read_u32_be(data + 8) : read_u32_le(data + 8);
    uint32_t root_offset = is_be ? read_u32_be(data + 12) : read_u32_le(data + 12);

    std::vector<std::string> keys;
    std::vector<std::string> strings;

    if (!parse_string_table(data, size, key_table_offset, is_be, keys)) return false;
    if (!parse_string_table(data, size, str_table_offset, is_be, strings)) return false;

    if (root_offset >= size) return false;
    BymlType root_type = static_cast<BymlType>(data[root_offset]);

    return parse_node(data, size, root_offset, root_type, is_be, keys, strings, out_root);
}

std::vector<uint8_t> BymlParser::create_synthetic_byml(const BymlValue &root) {
    std::vector<uint8_t> out;
    out.resize(16, 0);

    // Magic "YB" v7 LE
    out[0] = 'Y';
    out[1] = 'B';
    out[2] = 0x07;
    out[3] = 0x00;

    std::vector<std::string> keys;
    if (root.is_dict()) {
        for (const auto &pair : root.val_dict) {
            keys.push_back(pair.first);
        }
    }
    std::sort(keys.begin(), keys.end());

    std::vector<std::string> strings;
    if (root.is_dict()) {
        for (const auto &pair : root.val_dict) {
            if (pair.second.is_string()) {
                strings.push_back(pair.second.as_string());
            }
        }
    }

    auto write_str_table = [&out](const std::vector<std::string> &strs) -> uint32_t {
        if (strs.empty()) return 0;
        uint32_t table_start = static_cast<uint32_t>(out.size());
        uint32_t count = static_cast<uint32_t>(strs.size());

        out.push_back(static_cast<uint8_t>(BymlType::StringTable));
        out.push_back(count & 0xFF);
        out.push_back((count >> 8) & 0xFF);
        out.push_back((count >> 16) & 0xFF);

        size_t offsets_start = out.size();
        out.resize(offsets_start + (count + 1) * 4, 0);

        uint32_t cur_data_offset = 4 + (count + 1) * 4;
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(out.data() + offsets_start + i * 4, &cur_data_offset, 4);
            size_t str_pos = out.size();
            out.resize(str_pos + strs[i].size() + 1);
            std::memcpy(out.data() + str_pos, strs[i].c_str(), strs[i].size() + 1);
            cur_data_offset += static_cast<uint32_t>(strs[i].size() + 1);
        }
        std::memcpy(out.data() + offsets_start + count * 4, &cur_data_offset, 4);

        while (out.size() % 4 != 0) out.push_back(0);
        return table_start;
    };

    uint32_t key_tbl_off = write_str_table(keys);
    uint32_t str_tbl_off = write_str_table(strings);

    uint32_t root_off = static_cast<uint32_t>(out.size());
    if (root.is_dict()) {
        uint32_t count = static_cast<uint32_t>(keys.size());
        out.push_back(static_cast<uint8_t>(BymlType::Dictionary));
        out.push_back(count & 0xFF);
        out.push_back((count >> 8) & 0xFF);
        out.push_back((count >> 16) & 0xFF);

        size_t entry_start = out.size();
        out.resize(entry_start + count * 8, 0);

        for (size_t i = 0; i < count; ++i) {
            const auto &k = keys[i];
            const auto &v = root.val_dict.at(k);
            uint8_t *entry = out.data() + entry_start + i * 8;

            uint32_t key_idx = static_cast<uint32_t>(i);
            entry[0] = key_idx & 0xFF;
            entry[1] = (key_idx >> 8) & 0xFF;
            entry[2] = (key_idx >> 16) & 0xFF;
            entry[3] = static_cast<uint8_t>(v.type);

            uint32_t raw_val = 0;
            if (v.is_bool()) raw_val = v.val_bool ? 1 : 0;
            else if (v.is_int()) raw_val = static_cast<uint32_t>(v.val_int);
            else if (v.is_float()) {
                float f = static_cast<float>(v.val_float);
                std::memcpy(&raw_val, &f, 4);
            } else if (v.is_string()) {
                auto it = std::find(strings.begin(), strings.end(), v.as_string());
                raw_val = (it != strings.end()) ? static_cast<uint32_t>(std::distance(strings.begin(), it)) : 0;
            }
            std::memcpy(entry + 4, &raw_val, 4);
        }
    }

    std::memcpy(out.data() + 4, &key_tbl_off, 4);
    std::memcpy(out.data() + 8, &str_tbl_off, 4);
    std::memcpy(out.data() + 12, &root_off, 4);

    return out;
}

// BNTX Parser
bool BntxParser::parse(const uint8_t *data, size_t size, std::vector<BntxTextureInfo> &out_textures) {
    out_textures.clear();
    if (!data || size < 32) return false;

    // Check "BNTX\0\0\0\0" magic
    if (std::memcmp(data, "BNTX\0\0\0\0", 8) != 0 && std::memcmp(data, "BNTX", 4) != 0) {
        return false;
    }

    uint32_t file_size = read_u32_le(data + 0x0C);
    if (file_size > size) return false;

    // Search for "NX  " sub-header and "BRTI" texture blocks
    for (size_t i = 16; i + 64 <= size; i += 4) {
        if (std::memcmp(data + i, "BRTI", 4) == 0) {
            BntxTextureInfo tex;
            tex.width = read_u32_le(data + i + 0x14);
            tex.height = read_u32_le(data + i + 0x18);
            tex.depth = read_u32_le(data + i + 0x1C);
            tex.mip_levels = read_u32_le(data + i + 0x20);
            tex.nvn_format = read_u32_le(data + i + 0x24);
            tex.data_size = read_u32_le(data + i + 0x28);

            // Name pointer
            uint32_t name_off = read_u32_le(data + i + 0x0C);
            if (name_off < size) {
                tex.name = reinterpret_cast<const char *>(data + name_off);
            } else {
                tex.name = "Texture_" + std::to_string(out_textures.size());
            }

            out_textures.push_back(tex);
        }
    }

    return true;
}

std::vector<uint8_t> BntxParser::create_synthetic_bntx(const std::vector<BntxTextureInfo> &textures) {
    std::vector<uint8_t> out;
    out.resize(64, 0);

    std::memcpy(out.data(), "BNTX\0\0\0\0", 8);
    out[8] = 0x00; out[9] = 0x04; // Version 4.0

    for (size_t idx = 0; idx < textures.size(); ++idx) {
        const auto &t = textures[idx];
        size_t block_start = out.size();
        out.resize(block_start + 64, 0);

        std::memcpy(out.data() + block_start, "BRTI", 4);
        uint32_t block_size = 64;
        std::memcpy(out.data() + block_start + 4, &block_size, 4);

        size_t name_start = out.size();
        out.resize(name_start + t.name.size() + 1, 0);
        std::memcpy(out.data() + name_start, t.name.c_str(), t.name.size() + 1);

        uint32_t name_off = static_cast<uint32_t>(name_start);
        std::memcpy(out.data() + block_start + 0x0C, &name_off, 4);
        std::memcpy(out.data() + block_start + 0x14, &t.width, 4);
        std::memcpy(out.data() + block_start + 0x18, &t.height, 4);
        std::memcpy(out.data() + block_start + 0x1C, &t.depth, 4);
        std::memcpy(out.data() + block_start + 0x20, &t.mip_levels, 4);
        std::memcpy(out.data() + block_start + 0x24, &t.nvn_format, 4);
        std::memcpy(out.data() + block_start + 0x28, &t.data_size, 4);
    }

    uint32_t total_sz = static_cast<uint32_t>(out.size());
    std::memcpy(out.data() + 0x0C, &total_sz, 4);
    return out;
}

// BFRES Parser
bool BfresParser::parse(const uint8_t *data, size_t size,
                        std::vector<BfresModelInfo> &out_models,
                        std::vector<BntxTextureInfo> &out_textures) {
    out_models.clear();
    out_textures.clear();

    if (!data || size < 32) return false;
    if (std::memcmp(data, "FRES", 4) != 0) return false;

    // Scan for FMDL blocks (models)
    for (size_t i = 16; i + 32 <= size; i += 4) {
        if (std::memcmp(data + i, "FMDL", 4) == 0) {
            BfresModelInfo mdl;
            uint32_t name_off = read_u32_le(data + i + 0x08);
            if (name_off < size) {
                mdl.name = reinterpret_cast<const char *>(data + name_off);
            } else {
                mdl.name = "Model_" + std::to_string(out_models.size());
            }
            mdl.vertex_count = read_u32_le(data + i + 0x10);
            mdl.index_count = read_u32_le(data + i + 0x14);
            out_models.push_back(mdl);
        }
    }

    // Parse embedded BNTX textures if present
    for (size_t i = 0; i + 32 <= size; ++i) {
        if (std::memcmp(data + i, "BNTX", 4) == 0) {
            BntxParser::parse(data + i, size - i, out_textures);
            break;
        }
    }

    return true;
}

std::vector<uint8_t> BfresParser::create_synthetic_bfres(
    const std::vector<BfresModelInfo> &models,
    const std::vector<BntxTextureInfo> &textures) {
    std::vector<uint8_t> out;
    out.resize(32, 0);

    std::memcpy(out.data(), "FRES    ", 8);

    for (const auto &m : models) {
        size_t block_start = out.size();
        out.resize(block_start + 32, 0);

        std::memcpy(out.data() + block_start, "FMDL", 4);
        size_t name_pos = out.size();
        out.resize(name_pos + m.name.size() + 1, 0);
        std::memcpy(out.data() + name_pos, m.name.c_str(), m.name.size() + 1);
        while (out.size() % 4 != 0) out.push_back(0);

        uint32_t name_off = static_cast<uint32_t>(name_pos);
        std::memcpy(out.data() + block_start + 0x08, &name_off, 4);
        std::memcpy(out.data() + block_start + 0x10, &m.vertex_count, 4);
        std::memcpy(out.data() + block_start + 0x14, &m.index_count, 4);
    }

    while (out.size() % 4 != 0) out.push_back(0);

    if (!textures.empty()) {
        auto bntx_data = BntxParser::create_synthetic_bntx(textures);
        out.insert(out.end(), bntx_data.begin(), bntx_data.end());
    }

    return out;
}

} // namespace totk::runtime
