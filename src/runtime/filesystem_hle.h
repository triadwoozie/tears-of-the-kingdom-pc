#pragma once

#include <string>

namespace totk::runtime {

class FileSystemHLE {
public:
    static FileSystemHLE &instance();

    bool initialize(const std::string &romfs_path, const std::string &save_dir);
    bool mount_romfs(const std::string &path);
    bool mount_save(const std::string &path);

    std::string resolve_romfs_path(const std::string &guest_path) const;
    std::string resolve_save_path(const std::string &guest_path) const;

private:
    FileSystemHLE();
    ~FileSystemHLE();

    std::string romfs_root_;
    std::string save_root_;
};

} // namespace totk::runtime
