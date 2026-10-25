#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace totk::runtime {

enum VolumeId {
    VOL_SAVE = 0,
    VOL_TEMP = 1,
    VOL_CACHE_0 = 2,
    VOL_CACHE_1 = 3,
    VOL_CACHE_2 = 4,
    VOL_CACHE_3 = 5,
};

class ZstdDecompressor {
public:
    ZstdDecompressor();
    ~ZstdDecompressor();

    ZstdDecompressor(const ZstdDecompressor &) = delete;
    ZstdDecompressor &operator=(const ZstdDecompressor &) = delete;

    // Load a dictionary buffer (e.g. from ZsDic.pack.zs or uncompressed dictionary data)
    bool load_dictionary(const void *dict_data, size_t dict_size);

    // Decompress a standard Zstd frame into out_decompressed
    static bool decompress_frame(const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed);

    // Compress data into a Zstd frame (convenience for tests)
    static bool compress_frame(const void *src, size_t src_size, std::vector<uint8_t> &out_compressed, int compression_level = 3);

    // Decompress using loaded dictionary
    bool decompress_with_dict(const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed);

    bool has_dictionary() const { return ddict_ != nullptr; }

private:
    void *dctx_ = nullptr;
    void *ddict_ = nullptr;
    std::vector<uint8_t> dict_buffer_;
};

inline uint32_t sarc_hash(std::string_view name, uint32_t hash_key = 0x65) {
    uint32_t hash = 0;
    for (char c : name) {
        hash = hash * hash_key + static_cast<uint8_t>(c);
    }
    return hash;
}

struct SarcFileEntry {
    std::string name;
    uint32_t name_hash = 0;
    size_t data_offset = 0; // absolute byte offset in archive buffer
    size_t data_size = 0;
};

class SarcArchive {
public:
    SarcArchive();
    ~SarcArchive();

    SarcArchive(const SarcArchive &) = delete;
    SarcArchive &operator=(const SarcArchive &) = delete;
    SarcArchive(SarcArchive &&) noexcept = default;
    SarcArchive &operator=(SarcArchive &&) noexcept = default;

    // Parse from memory buffer
    bool parse(const void *data, size_t size);
    bool parse(std::vector<uint8_t> data);

    // Queries
    bool has_file(std::string_view name) const;
    bool has_file_hash(uint32_t hash) const;

    // Retrieve file data
    bool get_file(std::string_view name, std::vector<uint8_t> &out_data) const;
    bool get_file_view(std::string_view name, const uint8_t *&out_data, size_t &out_size) const;
    bool get_file_by_hash(uint32_t hash, const uint8_t *&out_data, size_t &out_size) const;

    size_t get_file_count() const { return entries_.size(); }
    const std::vector<SarcFileEntry> &get_entries() const { return entries_; }
    const SarcFileEntry *get_entry(std::string_view name) const;
    const SarcFileEntry *get_entry_by_hash(uint32_t hash) const;

    // Helper to generate a compliant SARC archive
    static std::vector<uint8_t> create_archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files, uint32_t hash_key = 0x65);

    static uint32_t calculate_hash(std::string_view name, uint32_t hash_key = 0x65) {
        return sarc_hash(name, hash_key);
    }

private:
    std::vector<uint8_t> owned_data_;
    const uint8_t *data_ptr_ = nullptr;
    size_t total_size_ = 0;
    uint32_t hash_key_ = 0x65;
    std::vector<SarcFileEntry> entries_;
    std::unordered_map<uint32_t, size_t> hash_to_entry_idx_;
    std::unordered_map<std::string, size_t> name_to_entry_idx_;
};

class FileSystemHLE {
public:
    static FileSystemHLE &instance();

    bool initialize(const std::string &romfs_path = "work/romfs", const std::string &save_dir = "work/save");
    void shutdown();

    bool mount_volume(VolumeId vol, const std::string &host_dir);
    bool unmount_volume(VolumeId vol);

    bool mount_romfs(const std::string &path);
    bool mount_save(const std::string &path);

    std::string resolve_romfs_path(const std::string &guest_path) const;
    std::string resolve_save_path(const std::string &guest_path) const;

    // File operations
    bool read_romfs_file(const std::string &guest_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data);
    bool read_file(const std::string &host_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data);
    bool write_file(const std::string &host_path, const void *data, size_t size);

    // Transparent Zstandard (.zs) decompression
    bool read_compressed_romfs_file(const std::string &guest_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data);
    bool load_romfs_dictionary(const std::string &dict_guest_path = "ZsDic.pack.zs");

    // Fast SARC / Pack Archive indexing
    bool read_sarc_file(const std::string &archive_guest_path, const std::string &internal_file_path, std::vector<uint8_t> &out_data);
    bool read_sarc_file_slice(const std::string &archive_guest_path, const std::string &internal_file_path, uint64_t offset, size_t size, std::vector<uint8_t> &out_data);
    SarcArchive *get_or_open_sarc(const std::string &archive_guest_path);

    ZstdDecompressor &get_zstd_decompressor() { return zstd_; }

private:
    FileSystemHLE();
    ~FileSystemHLE();

    std::string romfs_root_;
    std::string save_root_;
    std::string volume_roots_[6];
    bool volume_mounted_[6]{};
    ZstdDecompressor zstd_;
    std::unordered_map<std::string, std::unique_ptr<SarcArchive>> sarc_cache_;
};

} // namespace totk::runtime
