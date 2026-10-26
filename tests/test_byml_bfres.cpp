#include "runtime/byml_bfres_parser.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

using namespace totk::runtime;

static void test_byml_parser_roundtrip() {
    std::cout << "[Test] Running test_byml_parser_roundtrip..." << std::endl;

    BymlValue root = BymlValue::make_dict();
    root.set("GameTitle", BymlValue("The Legend of Zelda: Tears of the Kingdom"));
    root.set("TargetFrameRate", BymlValue(60.0f));
    root.set("InternalHeight", BymlValue(1080));
    root.set("VsyncEnabled", BymlValue(true));

    auto byml_bytes = BymlParser::create_synthetic_byml(root);
    assert(!byml_bytes.empty());
    assert(byml_bytes[0] == 'Y' && byml_bytes[1] == 'B');

    BymlValue parsed_root;
    bool ok = BymlParser::parse(byml_bytes.data(), byml_bytes.size(), parsed_root);
    assert(ok);
    assert(parsed_root.is_dict());
    assert(parsed_root.has_key("GameTitle"));
    assert(parsed_root.has_key("TargetFrameRate"));
    assert(parsed_root.has_key("InternalHeight"));
    assert(parsed_root.has_key("VsyncEnabled"));

    assert(parsed_root.get("GameTitle")->as_string() == "The Legend of Zelda: Tears of the Kingdom");
    assert(parsed_root.get("InternalHeight")->as_int() == 1080);
    assert(std::fabs(parsed_root.get("TargetFrameRate")->as_float() - 60.0) < 1e-3);
    assert(parsed_root.get("VsyncEnabled")->as_bool() == true);

    std::cout << "  -> BYML v7 Serialization & Parsing: PASSED" << std::endl;
}

static void test_bntx_texture_container_parser() {
    std::cout << "[Test] Running test_bntx_texture_container_parser..." << std::endl;

    std::vector<BntxTextureInfo> input_textures;
    BntxTextureInfo tex1;
    tex1.name = "TitleScreen_Background_ASTC_8x8";
    tex1.width = 1920;
    tex1.height = 1080;
    tex1.depth = 1;
    tex1.mip_levels = 4;
    tex1.nvn_format = 0x2D; // ASTC 8x8
    tex1.data_size = 2073600;
    input_textures.push_back(tex1);

    BntxTextureInfo tex2;
    tex2.name = "UI_Cursor_RGBA8";
    tex2.width = 64;
    tex2.height = 64;
    tex2.depth = 1;
    tex2.mip_levels = 1;
    tex2.nvn_format = 0x19; // RGBA8
    tex2.data_size = 16384;
    input_textures.push_back(tex2);

    auto bntx_bytes = BntxParser::create_synthetic_bntx(input_textures);
    assert(!bntx_bytes.empty());
    assert(std::memcmp(bntx_bytes.data(), "BNTX", 4) == 0);

    std::vector<BntxTextureInfo> parsed_textures;
    bool ok = BntxParser::parse(bntx_bytes.data(), bntx_bytes.size(), parsed_textures);
    assert(ok);
    assert(parsed_textures.size() == 2);

    assert(parsed_textures[0].name == "TitleScreen_Background_ASTC_8x8");
    assert(parsed_textures[0].width == 1920);
    assert(parsed_textures[0].height == 1080);
    assert(parsed_textures[0].mip_levels == 4);
    assert(parsed_textures[0].nvn_format == 0x2D);

    assert(parsed_textures[1].name == "UI_Cursor_RGBA8");
    assert(parsed_textures[1].width == 64);
    assert(parsed_textures[1].height == 64);
    assert(parsed_textures[1].nvn_format == 0x19);

    std::cout << "  -> BNTX Texture Container Parser: PASSED" << std::endl;
}

static void test_bfres_model_and_embedded_textures() {
    std::cout << "[Test] Running test_bfres_model_and_embedded_textures..." << std::endl;

    std::vector<BfresModelInfo> input_models;
    BfresModelInfo mdl;
    mdl.name = "Player_Hero_Link";
    mdl.vertex_count = 15420;
    mdl.index_count = 30840;
    input_models.push_back(mdl);

    std::vector<BntxTextureInfo> input_textures;
    BntxTextureInfo tex;
    tex.name = "Link_Skin_Diffuse";
    tex.width = 1024;
    tex.height = 1024;
    tex.nvn_format = 0x2D;
    input_textures.push_back(tex);

    auto bfres_bytes = BfresParser::create_synthetic_bfres(input_models, input_textures);
    assert(!bfres_bytes.empty());
    assert(std::memcmp(bfres_bytes.data(), "FRES", 4) == 0);

    std::vector<BfresModelInfo> parsed_models;
    std::vector<BntxTextureInfo> parsed_textures;
    bool ok = BfresParser::parse(bfres_bytes.data(), bfres_bytes.size(), parsed_models, parsed_textures);
    assert(ok);
    assert(parsed_models.size() == 1);
    assert(parsed_models[0].name == "Player_Hero_Link");
    assert(parsed_models[0].vertex_count == 15420);
    assert(parsed_models[0].index_count == 30840);

    assert(parsed_textures.size() == 1);
    assert(parsed_textures[0].name == "Link_Skin_Diffuse");
    assert(parsed_textures[0].width == 1024);
    assert(parsed_textures[0].height == 1024);

    std::cout << "  -> BFRES Model & Embedded Texture Parser: PASSED" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  BYML v7 & BFRES/BNTX Container Parsers Tests" << std::endl;
    std::cout << "==================================================" << std::endl;

    test_byml_parser_roundtrip();
    test_bntx_texture_container_parser();
    test_bfres_model_and_embedded_textures();

    std::cout << "\nAll BYML & BFRES/BNTX unit tests PASSED successfully!" << std::endl;
    return 0;
}
