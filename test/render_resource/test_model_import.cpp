#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/core/utils/path_utils.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/render_resource/image_decode.h"
#include "engine/function/render/render_resource/model_importer.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi_command_list.h"

#include <array>
#include <cstring>
#include <fstream>

TEST_CASE("DDS decoding preserves BC1 color and BC5 channels", "[render_resource][dds]") {
    test_utils::TestContext::reset();
    std::vector<uint8_t> dds(144, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    auto word = [&](size_t offset, uint32_t value) { std::memcpy(dds.data() + offset, &value, 4); };
    word(4, 124); word(8, 0x81007); word(12, 4); word(16, 4); word(20, 8);
    word(76, 32); word(80, 4); word(108, 0x1000);
    std::memcpy(dds.data() + 84, "DXT1", 4);
    dds[128] = 0x00; dds[129] = 0xf8; // RGB565 solid red, selector 0.
    auto color = decode_image_rgba(std::span(dds.data(), 136));
    REQUIRE(color);
    CHECK(color->width == 4);
    CHECK(color->height == 4);
    for (size_t i = 0; i < color->pixels.size(); i += 4) {
        CHECK(color->pixels[i] == 255);
        CHECK(color->pixels[i + 1] == 0);
        CHECK(color->pixels[i + 2] == 0);
        CHECK(color->pixels[i + 3] == 255);
    }
    auto source_path = utils::get_engine_path() / "test/test_internal/assets/dds_roundtrip.dds";
    std::filesystem::create_directories(source_path.parent_path());
    {
        std::ofstream source(source_path, std::ios::binary);
        source.write(reinterpret_cast<const char*>(dds.data()), 136);
    }
    auto texture = std::make_shared<Texture>(source_path.string(), FORMAT_R8G8B8A8_UNORM);
    REQUIRE(texture->texture_);
    auto* assets = EngineContext::asset();
    const std::string asset_path = "/Game/dds_roundtrip.binasset";
    assets->save_asset(texture, asset_path);
    assets->unload_asset(texture->get_uid());
    std::filesystem::remove(source_path);
    texture = assets->load_asset<Texture>(asset_path);
    REQUIRE(texture);
    REQUIRE(texture->texture_);
    auto backend = EngineContext::rhi();
    auto readback = backend->create_buffer({.size = 64, .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(readback);
    auto command = backend->get_immediate_command();
    command->copy_texture_to_buffer(texture->texture_, {TEXTURE_ASPECT_COLOR, 0, 0, 1}, readback, 0);
    command->flush();
    auto* gpu_pixels = static_cast<const uint8_t*>(readback->map());
    REQUIRE(gpu_pixels);
    for (size_t i = 0; i < 64; i += 4) {
        CHECK(gpu_pixels[i] == 255);
        CHECK(gpu_pixels[i + 1] == 0);
        CHECK(gpu_pixels[i + 2] == 0);
        CHECK(gpu_pixels[i + 3] == 255);
    }
    readback->unmap();
    std::filesystem::remove(*assets->get_physical_path(asset_path));
    std::memcpy(dds.data() + 84, "ATI2", 4);
    word(20, 16);
    std::fill(dds.begin() + 128, dds.end(), uint8_t(0));
    dds[128] = 128; dds[136] = 64;
    auto channels = decode_image_rgba(dds);
    REQUIRE(channels);
    CHECK(channels->pixels[0] == 128);
    CHECK(channels->pixels[1] == 64);
    CHECK_FALSE(decode_image_rgba(std::span(dds.data(), 20)));
    test_utils::TestContext::reset();
}

TEST_CASE("Model import bakes nested transforms and mirrored instances", "[render_resource][import]") {
    test_utils::TestContext::reset();
    auto dir = utils::get_engine_path() / "test/test_internal/assets/import_transform";
    std::filesystem::create_directories(dir);
    // Positions, normals, UVs: one triangle shared by two transformed nodes.
    const std::array<float, 24> data = {0,0,0, 1,0,0, 0,1,0, 0,0,1, 0,0,1, 0,0,1, 0,0, 1,0, 0,1};
    {
        std::ofstream bin(dir / "triangle.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(data.data()), sizeof(data));
        std::ofstream gltf(dir / "triangle.gltf");
        gltf << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":96}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2}}]}],
"nodes":[{"translation":[10,0,0],"rotation":[0,0,0.70710678,0.70710678],"children":[1]},
{"translation":[0,5,0],"scale":[-2,3,1],"mesh":0},{"translation":[-4,0,0],"mesh":0}],
"scenes":[{"nodes":[0,2]}],"scene":0})";
    }
    ModelProcessSetting settings;
    settings.flip_uv = false;
    settings.tangent_space = true;
    ModelImporter importer;
    auto model = importer.import_model((dir / "triangle.gltf").string(), "/Game/import_transform/triangle", settings);
    REQUIRE(model);
    REQUIRE(model->get_slot_count() == 2);
    auto first = model->get_mesh(0);
    auto second = model->get_mesh(1);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->get_positions()[0].x == Catch::Approx(5).margin(0.0001));
    CHECK(first->get_positions()[0].y == Catch::Approx(0).margin(0.0001));
    CHECK(first->get_positions()[1].y == Catch::Approx(-2).margin(0.0001));
    CHECK(second->get_positions()[0].x == Catch::Approx(-4));
    const auto& positions = first->get_positions();
    const auto& indices = first->get_indices();
    auto face_normal = (positions[indices[1]] - positions[indices[0]]).cross(positions[indices[2]] - positions[indices[0]]).normalized();
    CHECK(face_normal.dot(first->get_normals()[indices[0]]) > 0.99f);
    CHECK(first->get_tangents()[0].w == Catch::Approx(-1));
    CHECK(first->get_uid() != second->get_uid());

    const std::string save_path = "/Game/import_transform/roundtrip.asset";
    auto* assets = EngineContext::asset();
    assets->save_asset(model, save_path);
    auto uid = model->get_uid();
    assets->unload_asset(uid);
    auto loaded = assets->load_asset<Model>(save_path);
    REQUIRE(loaded);
    REQUIRE(loaded->get_slot_count() == 2);
    CHECK(loaded->get_mesh(0)->get_positions()[0].x == Catch::Approx(5).margin(0.0001));
    CHECK(loaded->get_mesh(1)->get_positions()[0].x == Catch::Approx(-4));
    // Only remove this test's own files.
    for (const auto& path : {dir / "triangle.bin", dir / "triangle.gltf", dir / "roundtrip.asset"}) {
        std::filesystem::remove(path);
    }
    test_utils::cleanup_uuid_named_assets(dir, false);
    test_utils::TestContext::reset();
}
