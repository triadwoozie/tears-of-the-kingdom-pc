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

void test_zstd_decompression_and_transparent_romfs() {
    std::printf("[Test] Running test_zstd_decompression_and_transparent_romfs...\n");

    // 1. Test standard Zstd frame compression & decompression
    const std::string original_payload = "Tears of the Kingdom RomFS Zstandard Decompression Test String 2026";
    std::vector<uint8_t> compressed;
    bool comp_ok = totk::runtime::ZstdDecompressor::compress_frame(
        original_payload.data(), original_payload.size(), compressed, 3
    );
    assert(comp_ok);
    assert(!compressed.empty());

    std::vector<uint8_t> decompressed;
    bool decomp_ok = totk::runtime::ZstdDecompressor::decompress_frame(
        compressed.data(), compressed.size(), decompressed
    );
    assert(decomp_ok);
    assert(decompressed.size() == original_payload.size());
    assert(std::memcmp(decompressed.data(), original_payload.data(), original_payload.size()) == 0);

    // 2. Test transparent RomFS reading of .zs files
    auto &fs = totk::runtime::FileSystemHLE::instance();
    fs.mount_romfs("work/test_romfs");

    // Write a .zs compressed file to work/test_romfs/Actor/ActorInfo.product.byml.zs
    bool write_ok = fs.write_file("work/test_romfs/Actor/ActorInfo.product.byml.zs", compressed.data(), compressed.size());
    assert(write_ok);

    // Transparent read of "romfs:/Actor/ActorInfo.product.byml" (without .zs extension!)
    std::vector<uint8_t> transparent_out;
    bool trans_ok = fs.read_romfs_file("romfs:/Actor/ActorInfo.product.byml", 0, 0, transparent_out);
    assert(trans_ok);
    assert(transparent_out.size() == original_payload.size());
    assert(std::memcmp(transparent_out.data(), original_payload.data(), original_payload.size()) == 0);

    // Offset reading of transparent .zs file
    std::vector<uint8_t> slice_out;
    bool slice_ok = fs.read_romfs_file("romfs:/Actor/ActorInfo.product.byml", 6, 14, slice_out);
    assert(slice_ok);
    assert(slice_out.size() == 14);
    assert(std::memcmp(slice_out.data(), "of the Kingdom", 14) == 0);

    // 3. Test dictionary initialization
    const char mock_dict[] = "TotKCustomDictionaryPayload_ZsDic.pack.zs";
    bool dict_ok = fs.get_zstd_decompressor().load_dictionary(mock_dict, sizeof(mock_dict));
    assert(dict_ok);
    assert(fs.get_zstd_decompressor().has_dictionary());

    std::printf("  -> Zstandard Frame & Transparent RomFS Decompression: PASSED\n");
}

void test_sarc_archive_indexing() {
    std::printf("[Test] Running test_sarc_archive_indexing...\n");

    // 1. Create a synthetic SARC archive with multiple assets
    const std::string file1_name = "Actor/Player.byml";
    const std::string file1_data = "BYML_MAGIC_HEADER_PLAYER_EQUIPMENT_DATA";

    const std::string file2_name = "System/Version.txt";
    const std::string file2_data = "TotK v1.4.3 Desktop Recomp Build 2026";

    const std::string file3_name = "Shader/sky.spv";
    const std::string file3_data = "\x03\x02\x23\x07SPIRV_BYTECODE_SAMPLE";

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        {file1_name, {file1_data.begin(), file1_data.end()}},
        {file2_name, {file2_data.begin(), file2_data.end()}},
        {file3_name, {file3_data.begin(), file3_data.end()}}
    };

    std::vector<uint8_t> sarc_raw = totk::runtime::SarcArchive::create_archive(files);
    assert(!sarc_raw.empty());

    // 2. Parse the archive
    totk::runtime::SarcArchive sarc;
    bool parse_ok = sarc.parse(sarc_raw.data(), sarc_raw.size());
    assert(parse_ok);
    assert(sarc.get_file_count() == 3);

    // 3. Query presence and hashes
    assert(sarc.has_file("Actor/Player.byml"));
    assert(sarc.has_file("System/Version.txt"));
    assert(sarc.has_file("Shader/sky.spv"));
    assert(!sarc.has_file("Missing/File.bin"));

    uint32_t hash_file2 = totk::runtime::sarc_hash("System/Version.txt");
    assert(sarc.has_file_hash(hash_file2));

    // 4. File data extraction
    std::vector<uint8_t> read_f1;
    bool get_ok = sarc.get_file("Actor/Player.byml", read_f1);
    assert(get_ok);
    assert(read_f1.size() == file1_data.size());
    assert(std::memcmp(read_f1.data(), file1_data.data(), file1_data.size()) == 0);

    // Direct memory view
    const uint8_t *view_ptr = nullptr;
    size_t view_size = 0;
    bool view_ok = sarc.get_file_view("Shader/sky.spv", view_ptr, view_size);
    assert(view_ok);
    assert(view_size == file3_data.size());
    assert(std::memcmp(view_ptr, file3_data.data(), file3_data.size()) == 0);

    // Hash lookup
    const uint8_t *hash_view = nullptr;
    size_t hash_size = 0;
    bool hash_ok = sarc.get_file_by_hash(hash_file2, hash_view, hash_size);
    assert(hash_ok);
    assert(hash_size == file2_data.size());
    assert(std::memcmp(hash_view, file2_data.data(), file2_data.size()) == 0);

    // 5. Test FileSystemHLE transparent indexing via romfs:/Pack/*.pack/*
    auto &fs = totk::runtime::FileSystemHLE::instance();
    fs.mount_romfs("work/test_romfs");

    // Write uncompressed pack archive to work/test_romfs/Pack/Bootup.pack
    bool write_ok = fs.write_file("work/test_romfs/Pack/Bootup.pack", sarc_raw.data(), sarc_raw.size());
    assert(write_ok);

    std::vector<uint8_t> romfs_subfile;
    bool sub_ok = fs.read_romfs_file("romfs:/Pack/Bootup.pack/Actor/Player.byml", 0, 0, romfs_subfile);
    assert(sub_ok);
    assert(romfs_subfile.size() == file1_data.size());
    assert(std::memcmp(romfs_subfile.data(), file1_data.data(), file1_data.size()) == 0);

    // 6. Test transparent Zstd-compressed pack archive: TitleBG.pack.zs
    std::vector<uint8_t> compressed_sarc;
    bool comp_ok = totk::runtime::ZstdDecompressor::compress_frame(sarc_raw.data(), sarc_raw.size(), compressed_sarc, 3);
    assert(comp_ok);

    bool write_zs = fs.write_file("work/test_romfs/Pack/TitleBG.pack.zs", compressed_sarc.data(), compressed_sarc.size());
    assert(write_zs);

    // Access file inside TitleBG.pack (without specifying .zs!)
    std::vector<uint8_t> zs_subfile;
    bool zs_sub_ok = fs.read_romfs_file("romfs:/Pack/TitleBG.pack/System/Version.txt", 0, 0, zs_subfile);
    assert(zs_sub_ok);
    assert(zs_subfile.size() == file2_data.size());
    assert(std::memcmp(zs_subfile.data(), file2_data.data(), file2_data.size()) == 0);

    // 7. Test zero-copy partial slice querying inside SARC pack
    std::vector<uint8_t> partial_slice;
    bool slice_ok = fs.read_romfs_file("romfs:/Pack/Bootup.pack/System/Version.txt", 5, 11, partial_slice);
    assert(slice_ok);
    assert(partial_slice.size() == 11);
    assert(std::memcmp(partial_slice.data(), "v1.4.3 Desk", 11) == 0);

    std::vector<uint8_t> direct_slice;
    bool dslice_ok = fs.read_sarc_file_slice("romfs:/Pack/TitleBG.pack.zs", "System/Version.txt", 0, 4, direct_slice);
    assert(dslice_ok);
    assert(direct_slice.size() == 4);
    assert(std::memcmp(direct_slice.data(), "TotK", 4) == 0);

    std::printf("  -> SARC Table of Contents Parser & Fast Hash Indexer: PASSED\n");
}

void test_zstd_multi_dictionary_pack() {
    std::printf("[Test] Running test_zstd_multi_dictionary_pack...\n");
    auto &fs = totk::runtime::FileSystemHLE::instance();
    fs.mount_romfs("work/test_romfs");

    // 1. Create two synthetic dictionaries
    const std::string dict1_str = "ZSTD_DICTIONARY_PAYLOAD_ACTOR_SYSTEM_WORDS_V1";
    const std::string dict2_str = "ZSTD_DICTIONARY_PAYLOAD_MAP_TERRAIN_TOPOLOGY_V2";

    std::vector<std::pair<std::string, std::vector<uint8_t>>> dict_files = {
        {"zs/actor.dict", {dict1_str.begin(), dict1_str.end()}},
        {"zs/terrain.dict", {dict2_str.begin(), dict2_str.end()}}
    };

    // Pack into SARC archive
    std::vector<uint8_t> sarc_raw = totk::runtime::SarcArchive::create_archive(dict_files);
    assert(!sarc_raw.empty());

    // Compress with standard Zstd into ZsDic.pack.zs
    std::vector<uint8_t> compressed_pack;
    bool comp_pack_ok = totk::runtime::ZstdDecompressor::compress_frame(
        sarc_raw.data(), sarc_raw.size(), compressed_pack, 3
    );
    assert(comp_pack_ok);

    bool write_ok = fs.write_file("work/test_romfs/Pack/ZsDic.pack.zs", compressed_pack.data(), compressed_pack.size());
    assert(write_ok);

    // 2. Load dictionary pack into FileSystemHLE
    bool load_ok = fs.load_romfs_dictionary("Pack/ZsDic.pack.zs");
    assert(load_ok);
    assert(fs.get_zstd_decompressor().has_dictionary());
    assert(fs.get_zstd_decompressor().get_dictionary_count() >= 2);

    // 3. Compress sample data using dictionary 1
    const std::string test_payload = "Actor: Link (Player) equipment state with ZSTD_DICTIONARY_PAYLOAD_ACTOR_SYSTEM_WORDS_V1 compression";
    std::vector<uint8_t> compressed_with_dict;
    bool cdict_ok = totk::runtime::ZstdDecompressor::compress_with_dict(
        test_payload.data(), test_payload.size(),
        dict1_str.data(), dict1_str.size(),
        compressed_with_dict, 3
    );
    assert(cdict_ok);
    assert(!compressed_with_dict.empty());

    // 4. Decompress via FileSystemHLE's loaded named dictionary
    std::vector<uint8_t> decompressed_out;
    bool ddict_ok = fs.get_zstd_decompressor().decompress_with_named_dict(
        "zs/actor.dict",
        compressed_with_dict.data(), compressed_with_dict.size(), decompressed_out
    );
    assert(ddict_ok);
    assert(decompressed_out.size() == test_payload.size());
    assert(std::memcmp(decompressed_out.data(), test_payload.data(), test_payload.size()) == 0);

    std::printf("  -> Zstandard Multi-Dictionary Pack Loading & Decompression: PASSED\n");
}

int main() {
    std::printf("=====================================================\n");
    std::printf("  TotK-Recomp: Unit Test - FileSystem & IPC\n");
    std::printf("=====================================================\n");

    test_filesystem_operations();
    test_ipc_services();
    test_zstd_decompression_and_transparent_romfs();
    test_sarc_archive_indexing();
    test_zstd_multi_dictionary_pack();

    std::printf("\nAll FileSystem and IPC verification tests PASSED!\n");
    return 0;
}
