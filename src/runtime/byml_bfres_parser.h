#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace totk::runtime {

enum class BymlType : uint8_t {
    Null = 0x00,
    StringTable = 0x01,
    PathTable = 0x02,
    String = 0xA0,
    Binary = 0xA1,
    Array = 0xC0,
    Dictionary = 0xC1,
    Bool = 0xD0,
    Int32 = 0xD1,
    Float32 = 0xD2,
    UInt32 = 0xD3,
    Int64 = 0xD4,
    UInt64 = 0xD5,
    Float64 = 0xD6,
};

class BymlValue {
public:
    BymlType type = BymlType::Null;

    bool val_bool = false;
    int64_t val_int = 0;
    uint64_t val_uint = 0;
    double val_float = 0.0;
    std::string val_string;
    std::vector<BymlValue> val_array;
    std::map<std::string, BymlValue> val_dict;

    BymlValue() = default;
    BymlValue(bool b) : type(BymlType::Bool), val_bool(b) {}
    BymlValue(int32_t i) : type(BymlType::Int32), val_int(i) {}
    BymlValue(uint32_t u) : type(BymlType::UInt32), val_uint(u) {}
    BymlValue(float f) : type(BymlType::Float32), val_float(f) {}
    BymlValue(int64_t i) : type(BymlType::Int64), val_int(i) {}
    BymlValue(uint64_t u) : type(BymlType::UInt64), val_uint(u) {}
    BymlValue(double d) : type(BymlType::Float64), val_float(d) {}
    BymlValue(const std::string &s) : type(BymlType::String), val_string(s) {}
    BymlValue(const char *s) : type(BymlType::String), val_string(s) {}

    static BymlValue make_array() {
        BymlValue v;
        v.type = BymlType::Array;
        return v;
    }

    static BymlValue make_dict() {
        BymlValue v;
        v.type = BymlType::Dictionary;
        return v;
    }

    bool is_null() const { return type == BymlType::Null; }
    bool is_bool() const { return type == BymlType::Bool; }
    bool is_int() const { return type == BymlType::Int32 || type == BymlType::Int64; }
    bool is_uint() const { return type == BymlType::UInt32 || type == BymlType::UInt64; }
    bool is_float() const { return type == BymlType::Float32 || type == BymlType::Float64; }
    bool is_string() const { return type == BymlType::String; }
    bool is_array() const { return type == BymlType::Array; }
    bool is_dict() const { return type == BymlType::Dictionary; }

    bool as_bool(bool def = false) const { return is_bool() ? val_bool : def; }
    int64_t as_int(int64_t def = 0) const { return is_int() ? val_int : (is_uint() ? static_cast<int64_t>(val_uint) : def); }
    double as_float(double def = 0.0) const { return is_float() ? val_float : (is_int() ? static_cast<double>(val_int) : def); }
    const std::string &as_string() const { return val_string; }

    bool has_key(const std::string &key) const {
        if (!is_dict()) return false;
        return val_dict.find(key) != val_dict.end();
    }

    const BymlValue *get(const std::string &key) const {
        if (!is_dict()) return nullptr;
        auto it = val_dict.find(key);
        if (it != val_dict.end()) return &it->second;
        return nullptr;
    }

    void set(const std::string &key, const BymlValue &val) {
        if (!is_dict()) type = BymlType::Dictionary;
        val_dict[key] = val;
    }

    void push(const BymlValue &val) {
        if (!is_array()) type = BymlType::Array;
        val_array.push_back(val);
    }
};

class BymlParser {
public:
    static bool parse(const uint8_t *data, size_t size, BymlValue &out_root);
    static std::vector<uint8_t> create_synthetic_byml(const BymlValue &root);
};

struct BntxTextureInfo {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mip_levels = 1;
    uint32_t nvn_format = 0; // NVNformat enum
    uint32_t data_size = 0;
    std::vector<uint8_t> data;
};

class BntxParser {
public:
    static bool parse(const uint8_t *data, size_t size, std::vector<BntxTextureInfo> &out_textures);
    static std::vector<uint8_t> create_synthetic_bntx(const std::vector<BntxTextureInfo> &textures);
};

struct BfresModelInfo {
    std::string name;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

class BfresParser {
public:
    static bool parse(const uint8_t *data, size_t size,
                      std::vector<BfresModelInfo> &out_models,
                      std::vector<BntxTextureInfo> &out_textures);
    static std::vector<uint8_t> create_synthetic_bfres(
        const std::vector<BfresModelInfo> &models,
        const std::vector<BntxTextureInfo> &textures);
};

} // namespace totk::runtime
