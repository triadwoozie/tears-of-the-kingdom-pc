#include "filesystem_hle.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <zstd.h>

namespace totk::runtime {

namespace fs = std::filesystem;

// =============================================================================
// ZstdDecompressor
// =============================================================================

ZstdDecompressor::ZstdDecompressor() {
    dctx_ = ZSTD_createDCtx();
}

ZstdDecompressor::~ZstdDecompressor() {
    for (auto &[_, d] : dict_map_) {
        if (d) {
            ZSTD_freeDDict(reinterpret_cast<ZSTD_DDict *>(d));
        }
    }
    dict_map_.clear();
    if (ddict_) {
        ZSTD_freeDDict(reinterpret_cast<ZSTD_DDict *>(ddict_));
        ddict_ = nullptr;
    }
    if (dctx_) {
        ZSTD_freeDCtx(reinterpret_cast<ZSTD_DCtx *>(dctx_));
        dctx_ = nullptr;
    }
}

bool ZstdDecompressor::load_dictionary(const void *dict_data, size_t dict_size) {
    if (!dict_data || dict_size == 0) return false;

    if (ddict_) {
        ZSTD_freeDDict(reinterpret_cast<ZSTD_DDict *>(ddict_));
        ddict_ = nullptr;
    }

    dict_buffer_.assign(reinterpret_cast<const uint8_t *>(dict_data),
                        reinterpret_cast<const uint8_t *>(dict_data) + dict_size);

    ddict_ = ZSTD_createDDict(dict_buffer_.data(), dict_buffer_.size());
    return (ddict_ != nullptr);
}

bool ZstdDecompressor::load_dictionary_with_id(unsigned dict_id, const void *dict_data, size_t dict_size) {
    if (!dict_data || dict_size == 0) return false;

    dict_buffers_.emplace_back(reinterpret_cast<const uint8_t *>(dict_data),
                               reinterpret_cast<const uint8_t *>(dict_data) + dict_size);
    const auto &buf = dict_buffers_.back();
    void *d = ZSTD_createDDict(buf.data(), buf.size());
    if (!d) return false;

    dict_map_[dict_id] = d;
    return true;
}

bool ZstdDecompressor::load_dictionary_with_name(const std::string &name, const void *dict_data, size_t dict_size) {
    if (!dict_data || dict_size == 0) return false;

    dict_buffers_.emplace_back(reinterpret_cast<const uint8_t *>(dict_data),
                               reinterpret_cast<const uint8_t *>(dict_data) + dict_size);
    const auto &buf = dict_buffers_.back();
    void *d = ZSTD_createDDict(buf.data(), buf.size());
    if (!d) return false;

    named_dict_map_[name] = d;
    return true;
}

bool ZstdDecompressor::decompress_with_dict_id(unsigned dict_id, const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed) {
    auto it = dict_map_.find(dict_id);
    if (it == dict_map_.end() || !it->second || !dctx_) return false;
    unsigned long long const rSize = ZSTD_getFrameContentSize(src, src_size);
    if (rSize == ZSTD_CONTENTSIZE_ERROR) return false;

    size_t target_size = (rSize == ZSTD_CONTENTSIZE_UNKNOWN) ? (src_size * 4 + 65536) : static_cast<size_t>(rSize);
    out_decompressed.resize(target_size);

    size_t const dSize = ZSTD_decompress_usingDDict(
        reinterpret_cast<ZSTD_DCtx *>(dctx_),
        out_decompressed.data(), target_size,
        src, src_size,
        reinterpret_cast<const ZSTD_DDict *>(it->second)
    );
    if (ZSTD_isError(dSize)) return false;
    out_decompressed.resize(dSize);
    return true;
}

bool ZstdDecompressor::decompress_with_named_dict(const std::string &name, const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed) {
    auto it = named_dict_map_.find(name);
    if (it == named_dict_map_.end() || !it->second || !dctx_) return false;
    unsigned long long const rSize = ZSTD_getFrameContentSize(src, src_size);
    if (rSize == ZSTD_CONTENTSIZE_ERROR) return false;

    size_t target_size = (rSize == ZSTD_CONTENTSIZE_UNKNOWN) ? (src_size * 4 + 65536) : static_cast<size_t>(rSize);
    out_decompressed.resize(target_size);

    size_t const dSize = ZSTD_decompress_usingDDict(
        reinterpret_cast<ZSTD_DCtx *>(dctx_),
        out_decompressed.data(), target_size,
        src, src_size,
        reinterpret_cast<const ZSTD_DDict *>(it->second)
    );
    if (ZSTD_isError(dSize)) return false;
    out_decompressed.resize(dSize);
    return true;
}

bool ZstdDecompressor::decompress_frame(const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed) {
    if (!src || src_size == 0) return false;

    unsigned long long const rSize = ZSTD_getFrameContentSize(src, src_size);
    if (rSize == ZSTD_CONTENTSIZE_ERROR) return false;

    size_t target_size = (rSize == ZSTD_CONTENTSIZE_UNKNOWN) ? (src_size * 4 + 65536) : static_cast<size_t>(rSize);
    out_decompressed.resize(target_size);

    size_t const dSize = ZSTD_decompress(out_decompressed.data(), target_size, src, src_size);
    if (ZSTD_isError(dSize)) return false;

    out_decompressed.resize(dSize);
    return true;
}

bool ZstdDecompressor::compress_frame(const void *src, size_t src_size, std::vector<uint8_t> &out_compressed, int compression_level) {
    if (!src || src_size == 0) return false;

    size_t const bound = ZSTD_compressBound(src_size);
    out_compressed.resize(bound);

    size_t const cSize = ZSTD_compress(out_compressed.data(), bound, src, src_size, compression_level);
    if (ZSTD_isError(cSize)) return false;

    out_compressed.resize(cSize);
    return true;
}

bool ZstdDecompressor::compress_with_dict(const void *src, size_t src_size, const void *dict_data, size_t dict_size, std::vector<uint8_t> &out_compressed, int compression_level) {
    if (!src || src_size == 0 || !dict_data || dict_size == 0) return false;

    ZSTD_CDict *cdict = ZSTD_createCDict(dict_data, dict_size, compression_level);
    if (!cdict) return false;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) {
        ZSTD_freeCDict(cdict);
        return false;
    }

    size_t const bound = ZSTD_compressBound(src_size);
    out_compressed.resize(bound);

    size_t const cSize = ZSTD_compress_usingCDict(cctx, out_compressed.data(), bound, src, src_size, cdict);
    ZSTD_freeCCtx(cctx);
    ZSTD_freeCDict(cdict);

    if (ZSTD_isError(cSize)) return false;

    out_compressed.resize(cSize);
    return true;
}

bool ZstdDecompressor::decompress_with_dict(const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed) {
    if (!src || src_size == 0) return false;
    if (!dctx_) return false;

    unsigned dict_id = ZSTD_getDictID_fromFrame(src, src_size);
    const void *active_ddict = nullptr;
    if (dict_id != 0) {
        auto it = dict_map_.find(dict_id);
        if (it != dict_map_.end()) {
            active_ddict = it->second;
        }
    }
    if (!active_ddict) {
        active_ddict = ddict_;
    }
    if (!active_ddict && !dict_map_.empty()) {
        active_ddict = dict_map_.begin()->second;
    }

    if (!active_ddict) {
        return decompress_frame(src, src_size, out_decompressed);
    }

    unsigned long long const rSize = ZSTD_getFrameContentSize(src, src_size);
    if (rSize == ZSTD_CONTENTSIZE_ERROR) return false;

    size_t target_size = (rSize == ZSTD_CONTENTSIZE_UNKNOWN) ? (src_size * 4 + 65536) : static_cast<size_t>(rSize);
    out_decompressed.resize(target_size);

    size_t dSize = ZSTD_decompress_usingDDict(
        reinterpret_cast<ZSTD_DCtx *>(dctx_),
        out_decompressed.data(), target_size,
        src, src_size,
        reinterpret_cast<const ZSTD_DDict *>(active_ddict)
    );

    if (ZSTD_isError(dSize) && !dict_map_.empty()) {
        for (const auto &[_, d] : dict_map_) {
            if (d == active_ddict) continue;
            dSize = ZSTD_decompress_usingDDict(
                reinterpret_cast<ZSTD_DCtx *>(dctx_),
                out_decompressed.data(), target_size,
                src, src_size,
                reinterpret_cast<const ZSTD_DDict *>(d)
            );
            if (!ZSTD_isError(dSize)) break;
        }
    }

    if (ZSTD_isError(dSize)) return false;

    out_decompressed.resize(dSize);
    return true;
}

// =============================================================================
// SarcArchive
// =============================================================================

SarcArchive::SarcArchive() = default;
SarcArchive::~SarcArchive() = default;

bool SarcArchive::parse(std::vector<uint8_t> data) {
    owned_data_ = std::move(data);
    return parse(owned_data_.data(), owned_data_.size());
}

bool SarcArchive::parse(const void *data, size_t size) {
    entries_.clear();
    hash_to_entry_idx_.clear();
    name_to_entry_idx_.clear();

    if (!data || size < 0x28) {
        return false;
    }

    data_ptr_ = static_cast<const uint8_t *>(data);
    total_size_ = size;

    // 1. Verify SARC Header (0x14 bytes)
    if (std::memcmp(data_ptr_, "SARC", 4) != 0) {
        return false;
    }

    uint16_t header_len = *reinterpret_cast<const uint16_t *>(data_ptr_ + 4);
    uint16_t bom = *reinterpret_cast<const uint16_t *>(data_ptr_ + 6);
    if (header_len != 0x14 || bom != 0xFEFF) {
        return false;
    }

    uint32_t file_size = *reinterpret_cast<const uint32_t *>(data_ptr_ + 8);
    uint32_t data_offset = *reinterpret_cast<const uint32_t *>(data_ptr_ + 12);
    if (file_size > size || data_offset > size) {
        return false;
    }

    // 2. Verify SFAT Header (starts at 0x14, 0x0C bytes)
    size_t sfat_offset = 0x14;
    if (sfat_offset + 12 > size || std::memcmp(data_ptr_ + sfat_offset, "SFAT", 4) != 0) {
        return false;
    }

    uint16_t sfat_hdr_len = *reinterpret_cast<const uint16_t *>(data_ptr_ + sfat_offset + 4);
    uint16_t node_count = *reinterpret_cast<const uint16_t *>(data_ptr_ + sfat_offset + 6);
    uint32_t hash_key = *reinterpret_cast<const uint32_t *>(data_ptr_ + sfat_offset + 8);
    if (sfat_hdr_len != 0x0C) {
        return false;
    }
    hash_key_ = hash_key;

    // 3. SFAT Nodes table: node_count * 16 bytes
    size_t nodes_offset = sfat_offset + 12;
    size_t nodes_total_len = static_cast<size_t>(node_count) * 16;
    if (nodes_offset + nodes_total_len > size) {
        return false;
    }

    // 4. SFNT Header (8 bytes) + String pool
    size_t sfnt_offset = nodes_offset + nodes_total_len;
    if (sfnt_offset + 8 > size || std::memcmp(data_ptr_ + sfnt_offset, "SFNT", 4) != 0) {
        return false;
    }

    const char *sfnt_str_pool = reinterpret_cast<const char *>(data_ptr_ + sfnt_offset + 8);
    size_t str_pool_max = (data_offset > (sfnt_offset + 8)) ? (data_offset - (sfnt_offset + 8)) : (size - (sfnt_offset + 8));

    entries_.reserve(node_count);
    for (uint16_t i = 0; i < node_count; ++i) {
        size_t node_ptr = nodes_offset + i * 16;
        uint32_t name_hash = *reinterpret_cast<const uint32_t *>(data_ptr_ + node_ptr);
        uint32_t name_offset_flags = *reinterpret_cast<const uint32_t *>(data_ptr_ + node_ptr + 4);
        uint32_t data_start = *reinterpret_cast<const uint32_t *>(data_ptr_ + node_ptr + 8);
        uint32_t data_end = *reinterpret_cast<const uint32_t *>(data_ptr_ + node_ptr + 12);

        SarcFileEntry entry{};
        entry.name_hash = name_hash;
        entry.data_offset = data_offset + data_start;
        entry.data_size = (data_end >= data_start) ? (data_end - data_start) : 0;

        if (entry.data_offset + entry.data_size > size) {
            continue; // Out of bounds safety guard
        }

        // Bit 24: has file name in SFNT
        if ((name_offset_flags & 0x01000000) != 0) {
            uint32_t str_off = (name_offset_flags & 0x00FFFFFF) * 4;
            if (str_off < str_pool_max) {
                entry.name = std::string(sfnt_str_pool + str_off);
            }
        }

        size_t idx = entries_.size();
        entries_.push_back(entry);
        hash_to_entry_idx_[name_hash] = idx;
        if (!entry.name.empty()) {
            name_to_entry_idx_[entry.name] = idx;
        }
    }

    return true;
}

bool SarcArchive::has_file(std::string_view name) const {
    return get_entry(name) != nullptr;
}

bool SarcArchive::has_file_hash(uint32_t hash) const {
    return get_entry_by_hash(hash) != nullptr;
}

const SarcFileEntry *SarcArchive::get_entry(std::string_view name) const {
    auto it = name_to_entry_idx_.find(std::string(name));
    if (it != name_to_entry_idx_.end()) {
        return &entries_[it->second];
    }
    // Fall back to hash query
    uint32_t h = sarc_hash(name, hash_key_);
    return get_entry_by_hash(h);
}

const SarcFileEntry *SarcArchive::get_entry_by_hash(uint32_t hash) const {
    auto it = hash_to_entry_idx_.find(hash);
    if (it != hash_to_entry_idx_.end()) {
        return &entries_[it->second];
    }
    return nullptr;
}

bool SarcArchive::get_file_view(std::string_view name, const uint8_t *&out_data, size_t &out_size) const {
    const auto *entry = get_entry(name);
    if (!entry || !data_ptr_) {
        out_data = nullptr;
        out_size = 0;
        return false;
    }
    out_data = data_ptr_ + entry->data_offset;
    out_size = entry->data_size;
    return true;
}

bool SarcArchive::get_file_by_hash(uint32_t hash, const uint8_t *&out_data, size_t &out_size) const {
    const auto *entry = get_entry_by_hash(hash);
    if (!entry || !data_ptr_) {
        out_data = nullptr;
        out_size = 0;
        return false;
    }
    out_data = data_ptr_ + entry->data_offset;
    out_size = entry->data_size;
    return true;
}

bool SarcArchive::get_file(std::string_view name, std::vector<uint8_t> &out_data) const {
    const uint8_t *view = nullptr;
    size_t sz = 0;
    if (!get_file_view(name, view, sz)) {
        out_data.clear();
        return false;
    }
    out_data.assign(view, view + sz);
    return true;
}

std::vector<uint8_t> SarcArchive::create_archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files, uint32_t hash_key) {
    uint16_t node_count = static_cast<uint16_t>(files.size());
    size_t sfat_size = 12 + node_count * 16;

    // SFNT string pool
    std::vector<uint8_t> string_pool;
    std::vector<uint32_t> name_flags;
    for (const auto &[name, _] : files) {
        uint32_t str_offset = static_cast<uint32_t>(string_pool.size());
        name_flags.push_back(0x01000000 | (str_offset / 4));
        for (char c : name) string_pool.push_back(static_cast<uint8_t>(c));
        string_pool.push_back(0); // Null terminator
        while (string_pool.size() % 4 != 0) string_pool.push_back(0); // 4-byte align
    }

    size_t sfnt_size = 8 + string_pool.size();
    size_t data_offset = 20 + sfat_size + sfnt_size;
    while (data_offset % 8 != 0) data_offset++; // 8-byte align data start

    // Calculate data ranges
    size_t current_data_offset = 0;
    std::vector<std::pair<uint32_t, uint32_t>> data_ranges;
    for (const auto &[_, content] : files) {
        uint32_t start = static_cast<uint32_t>(current_data_offset);
        uint32_t end = start + static_cast<uint32_t>(content.size());
        data_ranges.emplace_back(start, end);
        current_data_offset = end;
        while (current_data_offset % 4 != 0) current_data_offset++; // 4-byte align
    }

    uint32_t total_file_size = static_cast<uint32_t>(data_offset + current_data_offset);
    std::vector<uint8_t> out(total_file_size, 0);

    // 1. SARC Header
    std::memcpy(out.data(), "SARC", 4);
    *reinterpret_cast<uint16_t *>(out.data() + 4) = 0x14;
    *reinterpret_cast<uint16_t *>(out.data() + 6) = 0xFEFF;
    *reinterpret_cast<uint32_t *>(out.data() + 8) = total_file_size;
    *reinterpret_cast<uint32_t *>(out.data() + 12) = static_cast<uint32_t>(data_offset);
    *reinterpret_cast<uint16_t *>(out.data() + 16) = 0x0100;
    *reinterpret_cast<uint16_t *>(out.data() + 18) = 0x0000;

    // 2. SFAT Header
    size_t sfat_offset = 20;
    std::memcpy(out.data() + sfat_offset, "SFAT", 4);
    *reinterpret_cast<uint16_t *>(out.data() + sfat_offset + 4) = 0x0C;
    *reinterpret_cast<uint16_t *>(out.data() + sfat_offset + 6) = node_count;
    *reinterpret_cast<uint32_t *>(out.data() + sfat_offset + 8) = hash_key;

    // 3. SFAT Nodes
    size_t nodes_offset = sfat_offset + 12;
    for (size_t i = 0; i < files.size(); ++i) {
        size_t node_ptr = nodes_offset + i * 16;
        uint32_t nh = sarc_hash(files[i].first, hash_key);
        *reinterpret_cast<uint32_t *>(out.data() + node_ptr) = nh;
        *reinterpret_cast<uint32_t *>(out.data() + node_ptr + 4) = name_flags[i];
        *reinterpret_cast<uint32_t *>(out.data() + node_ptr + 8) = data_ranges[i].first;
        *reinterpret_cast<uint32_t *>(out.data() + node_ptr + 12) = data_ranges[i].second;
    }

    // 4. SFNT Header and String Pool
    size_t sfnt_offset = nodes_offset + node_count * 16;
    std::memcpy(out.data() + sfnt_offset, "SFNT", 4);
    *reinterpret_cast<uint16_t *>(out.data() + sfnt_offset + 4) = 0x08;
    *reinterpret_cast<uint16_t *>(out.data() + sfnt_offset + 6) = 0;
    std::memcpy(out.data() + sfnt_offset + 8, string_pool.data(), string_pool.size());

    // 5. File payload data
    for (size_t i = 0; i < files.size(); ++i) {
        size_t file_dest = data_offset + data_ranges[i].first;
        const auto &content = files[i].second;
        if (!content.empty()) {
            std::memcpy(out.data() + file_dest, content.data(), content.size());
        }
    }

    return out;
}

// =============================================================================
// FileSystemHLE
// =============================================================================

FileSystemHLE &FileSystemHLE::instance() {
    static FileSystemHLE f;
    return f;
}

FileSystemHLE::FileSystemHLE() = default;

FileSystemHLE::~FileSystemHLE() {
    shutdown();
}

bool FileSystemHLE::initialize(const std::string &romfs_path, const std::string &save_dir) {
    romfs_root_ = romfs_path;
    save_root_ = save_dir;

    mount_volume(VOL_SAVE, save_dir);
    mount_volume(VOL_TEMP, "work/temporary");
    mount_volume(VOL_CACHE_0, "work/cache-0");
    mount_volume(VOL_CACHE_1, "work/cache-1");

    return true;
}

void FileSystemHLE::shutdown() {
    for (int i = 0; i < 6; ++i) {
        unmount_volume(static_cast<VolumeId>(i));
    }
    sarc_cache_.clear();
}

bool FileSystemHLE::mount_volume(VolumeId vol, const std::string &host_dir) {
    if (vol < 0 || vol >= 6) return false;
    volume_roots_[vol] = host_dir;
    volume_mounted_[vol] = true;

    std::error_code ec;
    fs::create_directories(host_dir, ec);
    return true;
}

bool FileSystemHLE::unmount_volume(VolumeId vol) {
    if (vol < 0 || vol >= 6) return false;
    volume_mounted_[vol] = false;
    volume_roots_[vol].clear();
    return true;
}

bool FileSystemHLE::mount_romfs(const std::string &path) {
    romfs_root_ = path;
    return fs::exists(path);
}

bool FileSystemHLE::mount_save(const std::string &path) {
    save_root_ = path;
    std::error_code ec;
    return fs::create_directories(path, ec) || fs::exists(path);
}

std::string FileSystemHLE::resolve_romfs_path(const std::string &guest_path) const {
    std::string rel = guest_path;
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    if (rel.rfind("romfs:/", 0) == 0) rel = rel.substr(7);
    return (fs::path(romfs_root_) / rel).string();
}

std::string FileSystemHLE::resolve_save_path(const std::string &guest_path) const {
    std::string rel = guest_path;
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    if (rel.rfind("save:/", 0) == 0) rel = rel.substr(6);
    return (fs::path(save_root_) / rel).string();
}

bool FileSystemHLE::read_romfs_file(const std::string &guest_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data) {
    // Check if guest path refers to an internal archive sub-file (e.g. "Pack/Bootup.pack/Actor/Player.byml" or ".sarc/")
    size_t pack_pos = guest_path.find(".pack/");
    if (pack_pos == std::string::npos) {
        pack_pos = guest_path.find(".sarc/");
    }
    if (pack_pos != std::string::npos) {
        size_t ext_len = 5; // length of ".pack" or ".sarc"
        std::string archive_part = guest_path.substr(0, pack_pos + ext_len);
        std::string internal_part = guest_path.substr(pack_pos + ext_len + 1);
        if (read_sarc_file_slice(archive_part, internal_part, offset, size, out_data)) {
            return true;
        }
    }

    std::string host_path = resolve_romfs_path(guest_path);

    // 1. If direct file exists on disk
    if (fs::exists(host_path)) {
        if (host_path.size() >= 3 && host_path.substr(host_path.size() - 3) == ".zs") {
            return read_compressed_romfs_file(guest_path, offset, size, out_data);
        }
        return read_file(host_path, offset, size, out_data);
    }

    // 2. Check if transparent .zs version exists (e.g. ActorInfo.product.byml -> ActorInfo.product.byml.zs)
    std::string zs_host_path = host_path + ".zs";
    if (fs::exists(zs_host_path)) {
        return read_compressed_romfs_file(guest_path + ".zs", offset, size, out_data);
    }

    return false;
}

SarcArchive *FileSystemHLE::get_or_open_sarc(const std::string &archive_guest_path) {
    auto it = sarc_cache_.find(archive_guest_path);
    if (it != sarc_cache_.end()) {
        return it->second.get();
    }

    std::vector<uint8_t> archive_data;
    if (!read_romfs_file(archive_guest_path, 0, 0, archive_data)) {
        return nullptr;
    }

    auto sarc = std::make_unique<SarcArchive>();
    if (!sarc->parse(std::move(archive_data))) {
        return nullptr;
    }

    auto *ptr = sarc.get();
    sarc_cache_[archive_guest_path] = std::move(sarc);
    return ptr;
}

bool FileSystemHLE::read_sarc_file(const std::string &archive_guest_path, const std::string &internal_file_path, std::vector<uint8_t> &out_data) {
    return read_sarc_file_slice(archive_guest_path, internal_file_path, 0, 0, out_data);
}

bool FileSystemHLE::read_sarc_file_slice(const std::string &archive_guest_path, const std::string &internal_file_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data) {
    SarcArchive *sarc = get_or_open_sarc(archive_guest_path);
    if (!sarc) return false;
    const uint8_t *view = nullptr;
    size_t total_len = 0;
    if (!sarc->get_file_view(internal_file_path, view, total_len)) {
        return false;
    }
    if (offset >= total_len) {
        out_data.clear();
        return false;
    }
    size_t to_copy = (size == 0 || offset + size > total_len) ? (total_len - offset) : size;
    out_data.assign(view + offset, view + offset + to_copy);
    return true;
}

bool FileSystemHLE::read_compressed_romfs_file(const std::string &guest_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data) {
    std::string host_path = resolve_romfs_path(guest_path);
    std::vector<uint8_t> compressed_data;
    if (!read_file(host_path, 0, 0, compressed_data)) {
        return false;
    }

    std::vector<uint8_t> decompressed;
    if (zstd_.has_dictionary()) {
        if (!zstd_.decompress_with_dict(compressed_data.data(), compressed_data.size(), decompressed)) {
            if (!ZstdDecompressor::decompress_frame(compressed_data.data(), compressed_data.size(), decompressed)) {
                return false;
            }
        }
    } else {
        if (!ZstdDecompressor::decompress_frame(compressed_data.data(), compressed_data.size(), decompressed)) {
            return false;
        }
    }

    if (offset >= decompressed.size()) {
        out_data.clear();
        return false;
    }

    size_t to_copy = (size == 0 || offset + size > decompressed.size()) ? (decompressed.size() - offset) : size;
    out_data.assign(decompressed.begin() + offset, decompressed.begin() + offset + to_copy);
    return true;
}

bool FileSystemHLE::load_romfs_dictionary(const std::string &dict_guest_path) {
    std::string host_path = resolve_romfs_path(dict_guest_path);
    std::vector<uint8_t> dict_raw;
    if (!read_file(host_path, 0, 0, dict_raw)) {
        return false;
    }

    std::vector<uint8_t> uncompressed;
    const uint8_t *payload_ptr = dict_raw.data();
    size_t payload_size = dict_raw.size();

    // If dictionary itself is compressed (.zs), decompress it first
    if (dict_guest_path.size() >= 3 && dict_guest_path.substr(dict_guest_path.size() - 3) == ".zs") {
        if (ZstdDecompressor::decompress_frame(dict_raw.data(), dict_raw.size(), uncompressed)) {
            payload_ptr = uncompressed.data();
            payload_size = uncompressed.size();
        }
    }

    // Check if the payload is a SARC pack containing multiple dictionaries
    if (payload_size >= 4 && std::memcmp(payload_ptr, "SARC", 4) == 0) {
        SarcArchive sarc;
        if (sarc.parse(payload_ptr, payload_size)) {
            bool any_loaded = false;
            unsigned auto_id = 1;
            for (const auto &entry : sarc.get_entries()) {
                const uint8_t *file_data = nullptr;
                size_t file_size = 0;
                if (sarc.get_file_view(entry.name, file_data, file_size) && file_size > 0) {
                    unsigned dict_id = ZSTD_getDictID_fromDict(file_data, file_size);
                    if (dict_id == 0) {
                        dict_id = auto_id++;
                    }
                    if (zstd_.load_dictionary_with_id(dict_id, file_data, file_size)) {
                        any_loaded = true;
                    }
                    zstd_.load_dictionary_with_name(entry.name, file_data, file_size);
                }
            }
            if (any_loaded) return true;
        }
    }

    return zstd_.load_dictionary(payload_ptr, payload_size);
}

bool FileSystemHLE::read_file(const std::string &host_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data) {
    std::ifstream file(host_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    uint64_t file_size = file.tellg();
    if (offset >= file_size) return false;

    size_t to_read = (size == 0 || offset + size > file_size) ? (file_size - offset) : size;
    file.seekg(offset, std::ios::beg);

    out_data.resize(to_read);
    return file.read(reinterpret_cast<char *>(out_data.data()), to_read).good();
}

bool FileSystemHLE::write_file(const std::string &host_path, const void *data, size_t size) {
    fs::path p(host_path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    std::ofstream file(host_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    if (data && size > 0) {
        file.write(reinterpret_cast<const char *>(data), size);
    }
    return file.good();
}

} // namespace totk::runtime
