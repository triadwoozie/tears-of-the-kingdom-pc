#include "filesystem_hle.h"
#include <cstdio>
#include <filesystem>

namespace totk::runtime {

namespace fs = std::filesystem;

FileSystemHLE &FileSystemHLE::instance() {
    static FileSystemHLE f;
    return f;
}

FileSystemHLE::FileSystemHLE()
    : romfs_root_("work/romfs"), save_root_("local/saves") {}

FileSystemHLE::~FileSystemHLE() = default;

bool FileSystemHLE::initialize(const std::string &romfs_path, const std::string &save_dir) {
    mount_romfs(romfs_path);
    mount_save(save_dir);
    return true;
}

bool FileSystemHLE::mount_romfs(const std::string &path) {
    romfs_root_ = path;
    std::printf("[FileSystemHLE] Mounted RomFS root at: %s\n", romfs_root_.c_str());
    return true;
}

bool FileSystemHLE::mount_save(const std::string &path) {
    save_root_ = path;
    std::error_code ec;
    fs::create_directories(save_root_, ec);
    std::printf("[FileSystemHLE] Mounted SaveData directory at: %s\n", save_root_.c_str());
    return true;
}

std::string FileSystemHLE::resolve_romfs_path(const std::string &guest_path) const {
    fs::path p = fs::path(romfs_root_) / guest_path;
    return p.lexically_normal().string();
}

std::string FileSystemHLE::resolve_save_path(const std::string &guest_path) const {
    fs::path p = fs::path(save_root_) / guest_path;
    return p.lexically_normal().string();
}

} // namespace totk::runtime
