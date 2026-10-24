#include "filesystem_hle.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace totk::runtime {

namespace fs = std::filesystem;

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
    return read_file(host_path, offset, size, out_data);
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
