#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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

    ZstdDecompressor &get_zstd_decompressor() { return zstd_; }

private:
    FileSystemHLE();
    ~FileSystemHLE();

    std::string romfs_root_;
    std::string save_root_;
    std::string volume_roots_[6];
    bool volume_mounted_[6]{};
    ZstdDecompressor zstd_;
};

} // namespace totk::runtime
