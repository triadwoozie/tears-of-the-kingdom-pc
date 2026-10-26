static bool is_guest_resource_path(const std::string &path) {
    return !path.empty() &&
           (path.find('/') != std::string::npos ||
            path.find('\\') != std::string::npos ||
            path.find('.') != std::string::npos ||
            path.find(':') != std::string::npos);
}

static bool read_guest_path_string(uint64_t address, std::string &path) {
    auto &memory = totk::runtime::MemoryManager::instance();
    if (address < totk::runtime::GUEST_MEMORY_BASE ||
        address >= totk::runtime::GUEST_MEMORY_LIMIT) {
        return false;
    }

    const size_t readable = static_cast<size_t>(
        std::min<uint64_t>(4096, totk::runtime::GUEST_MEMORY_LIMIT - address));
    if (!memory.commit(address, readable)) return false;

    const char *string = reinterpret_cast<const char *>(address);
    path.clear();
    for (size_t i = 0; i < readable; ++i) {
        const unsigned char ch = static_cast<unsigned char>(string[i]);
        if (ch == 0) return is_guest_resource_path(path);
        if (ch < 0x20 || ch > 0x7e) return false;
        path.push_back(static_cast<char>(ch));
    }
    path.clear();
    return false;
}

static bool extract_guest_resource_path(const totk::recompiler::Arm64Context *context,
                                       std::string &path) {
    const uint64_t inputs[] = {context->x[1], context->x[2], context->x[3]};
    for (uint64_t input : inputs) {
        if (read_guest_path_string(input, path)) return true;
    }

    for (uint64_t input : inputs) {
        if (input < totk::runtime::GUEST_MEMORY_BASE ||
            input > totk::runtime::GUEST_MEMORY_LIMIT - 0x28) {
            continue;
        }
        auto &memory = totk::runtime::MemoryManager::instance();
        if (!memory.commit(input, 0x28)) continue;
        for (uint64_t offset : {0ULL, 8ULL, 0x10ULL, 0x18ULL, 0x20ULL}) {
            uint64_t candidate = *reinterpret_cast<const uint64_t *>(input + offset);
            if (read_guest_path_string(candidate, path)) return true;
        }
    }
    return false;
}

static bool guest_resource_path_is_null(const totk::recompiler::Arm64Context *context) {
    const uint64_t request = context->x[1];
    if (request == 0) return true;
    if (request < totk::runtime::GUEST_MEMORY_BASE ||
        request > totk::runtime::GUEST_MEMORY_LIMIT - sizeof(uint64_t)) {
        return false;
    }
    auto &memory = totk::runtime::MemoryManager::instance();
    return memory.commit(request, sizeof(uint64_t)) &&
           *reinterpret_cast<const uint64_t *>(request) == 0;
}

static uint64_t create_loaded_sead_resource(const std::vector<uint8_t> &data) {
    if (data.size() > UINT32_MAX) return 0;

    const uint64_t dummy = get_dummy_sead_resource();
    const uint64_t resource = hlr_bump_alloc(0x200, 0x20);
    const uint64_t payload = hlr_bump_alloc(std::max<size_t>(data.size(), 1), 0x10);
    if (!data.empty()) {
        std::memcpy(reinterpret_cast<void *>(payload), data.data(), data.size());
    }

    *reinterpret_cast<uint64_t *>(resource) = *reinterpret_cast<const uint64_t *>(dummy);
    *reinterpret_cast<uint64_t *>(resource + 0x28) = payload;
    *reinterpret_cast<uint32_t *>(resource + 0x30) = static_cast<uint32_t>(data.size());
    *reinterpret_cast<uint8_t *>(resource + 0x58) = 1;
    return resource;
}

static void hlr_sead_resource_load(totk::recompiler::Arm64Context *context) {
    const uint64_t return_address = context->x[30];
    std::string path;
    if (!extract_guest_resource_path(context, path)) {
        context->x[0] = guest_resource_path_is_null(context) ? get_dummy_sead_resource() : 0;
        context->pc = return_address;
        return;
    }

    std::vector<uint8_t> data;
    if (!totk::runtime::FileSystemHLE::instance().read_romfs_file(path, 0, 0, data)) {
        std::fprintf(stderr, "[HLR ResourceMgr] RomFS load failed for '%s' (LR=0x%llx)\n",
                     path.c_str(), static_cast<unsigned long long>(return_address));
        context->x[0] = 0;
        context->pc = return_address;
        return;
    }

    const uint64_t resource = create_loaded_sead_resource(data);
    if (resource == 0) {
        std::fprintf(stderr, "[HLR ResourceMgr] Resource too large: '%s' (%zu bytes)\n",
                     path.c_str(), data.size());
    }
    context->x[0] = resource;
    context->pc = return_address;
}
