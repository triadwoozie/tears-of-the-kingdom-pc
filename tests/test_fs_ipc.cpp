#include "runtime/filesystem_hle.h"
#include "runtime/services_hle.h"
#include "runtime/kernel_hle.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

void test_filesystem_operations() {
    std::printf("[Test] Running test_filesystem_operations...\n");

    auto &fs = totk::runtime::FileSystemHLE::instance();
    bool init_ok = fs.initialize("work/romfs", "work/save");
    assert(init_ok);

    // Test volume mounting
    bool mount_ok = fs.mount_volume(totk::runtime::VOL_TEMP, "work/temporary");
    assert(mount_ok);

    // Test writing a file
    const char test_data[] = "TotK-Recomp FileSystemHLE Test Payload 2026";
    size_t data_len = sizeof(test_data);
    bool write_ok = fs.write_file("work/temporary/test_file.bin", test_data, data_len);
    assert(write_ok);

    // Test reading back the file
    std::vector<uint8_t> read_buf;
    bool read_ok = fs.read_file("work/temporary/test_file.bin", 0, data_len, read_buf);
    assert(read_ok);
    assert(read_buf.size() == data_len);
    assert(std::memcmp(read_buf.data(), test_data, data_len) == 0);

    // Test offset reading
    std::vector<uint8_t> partial_buf;
    bool partial_ok = fs.read_file("work/temporary/test_file.bin", 12, 11, partial_buf);
    assert(partial_ok);
    assert(partial_buf.size() == 11);
    assert(std::memcmp(partial_buf.data(), "FileSystemH", 11) == 0);

    std::printf("  -> FileSystem Volume & I/O: PASSED\n");
}

void test_ipc_services() {
    std::printf("[Test] Running test_ipc_services...\n");

    auto &services = totk::runtime::ServicesHLE::instance();
    services.initialize();

    // 1. Connect to sm: port
    uint32_t sm_handle = 0;
    bool conn_ok = services.connect_port("sm:", &sm_handle);
    assert(conn_ok);
    assert(sm_handle != 0);

    // 2. Query allowed service "fsp-srv" via Hipc request
    uint8_t ipc_buf[256]{};
    // Hipc message header: Type=4 (Request), 8 data words
    *reinterpret_cast<uint32_t *>(ipc_buf) = 4 | (8 << 16);
    // CMIF header at offset 0x10: SFCI (0x49434653), cmd_id = 1 (GetService)
    *reinterpret_cast<uint32_t *>(ipc_buf + 0x10) = 0x49434653;
    *reinterpret_cast<uint32_t *>(ipc_buf + 0x18) = 1; // GetService
    std::memcpy(ipc_buf + 0x20, "fsp-srv\0", 8);

    uint32_t res = services.dispatch_ipc(sm_handle, ipc_buf, sizeof(ipc_buf));
    assert(res == totk::runtime::ResultSuccess);

    // 3. Test APM service session
    uint32_t apm_handle = 0;
    bool apm_conn = services.connect_port("apm", &apm_handle);
    assert(apm_conn);

    // Command 0: OpenSession
    std::memset(ipc_buf, 0, sizeof(ipc_buf));
    *reinterpret_cast<uint32_t *>(ipc_buf) = 4 | (8 << 16);
    *reinterpret_cast<uint32_t *>(ipc_buf + 0x10) = 0x49434653;
    *reinterpret_cast<uint32_t *>(ipc_buf + 0x18) = 0; // OpenSession

    res = services.dispatch_ipc(apm_handle, ipc_buf, sizeof(ipc_buf));
    assert(res == totk::runtime::ResultSuccess);

    std::printf("  -> Horizon IPC Service Dispatch: PASSED\n");
}

int main() {
    std::printf("=====================================================\n");
    std::printf("  TotK-Recomp: Unit Test - FileSystem & IPC\n");
    std::printf("=====================================================\n");

    test_filesystem_operations();
    test_ipc_services();

    std::printf("\nAll FileSystem and IPC verification tests PASSED!\n");
    return 0;
}
