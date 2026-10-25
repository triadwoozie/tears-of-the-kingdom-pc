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

bool ZstdDecompressor::decompress_with_dict(const void *src, size_t src_size, std::vector<uint8_t> &out_decompressed) {
    if (!src || src_size == 0) return false;
    if (!dctx_) return false;

    if (!ddict_) {
        return decompress_frame(src, src_size, out_decompressed);
    }

    unsigned long long const rSize = ZSTD_getFrameContentSize(src, src_size);
    if (rSize == ZSTD_CONTENTSIZE_ERROR) return false;

    size_t target_size = (rSize == ZSTD_CONTENTSIZE_UNKNOWN) ? (src_size * 4 + 65536) : static_cast<size_t>(rSize);
    out_decompressed.resize(target_size);

    size_t const dSize = ZSTD_decompress_usingDDict(
        reinterpret_cast<ZSTD_DCtx *>(dctx_),
        out_decompressed.data(), target_size,
        src, src_size,
        reinterpret_cast<const ZSTD_DDict *>(ddict_)
    );

    if (ZSTD_isError(dSize)) return false;

    out_decompressed.resize(dSize);
    return true;
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

    // If dictionary itself is compressed (.zs), decompress it first
    if (dict_guest_path.size() >= 3 && dict_guest_path.substr(dict_guest_path.size() - 3) == ".zs") {
        std::vector<uint8_t> uncompressed_dict;
        if (ZstdDecompressor::decompress_frame(dict_raw.data(), dict_raw.size(), uncompressed_dict)) {
            return zstd_.load_dictionary(uncompressed_dict.data(), uncompressed_dict.size());
        }
    }

    return zstd_.load_dictionary(dict_raw.data(), dict_raw.size());
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
