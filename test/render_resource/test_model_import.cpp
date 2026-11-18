#include "test/test_assertions.h"
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/render_resource/image_decode.h"
#include "engine/function/render/render_resource/model_importer.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/render/render_pass/mesh_pass.h"
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
    CHECK(test_utils::image_matches(color->pixels, 4, [](auto, auto) { return std::array<uint8_t,4>{255,0,0,255}; }));
    // The second and third mips use different endpoints from mip 0.
    dds.resize(152);
    word(8, 0xa1007); word(28, 3); word(108, 0x401008);
    dds[136] = 0xe0; dds[137] = 0x07;
    dds[144] = 0x1f;
    auto source_path = test_utils::TestContext::project_dir() / "assets/dds_roundtrip.dds";
    std::filesystem::create_directories(source_path.parent_path());
    {
        std::ofstream source(source_path, std::ios::binary);
        source.write(reinterpret_cast<const char*>(dds.data()), dds.size());
    }
    auto texture = std::make_shared<Texture>(source_path.string(), FORMAT_R8G8B8A8_SRGB);
    REQUIRE(texture->texture_);
    auto* assets = EngineContext::asset();
    const std::string asset_path = "/Game/dds_roundtrip.binasset";
    assets->save_asset(texture, asset_path);
    const auto stored_path = assets->get_physical_path(asset_path);
    REQUIRE(stored_path);
    {
        std::ifstream input(*stored_path, std::ios::binary);
        std::string encoded{std::istreambuf_iterator<char>(input), {}};
        CHECK(encoded.find("BCZ1") != std::string::npos);
        CHECK(encoded.find("DDS ") == std::string::npos);
    }
    assets->unload_asset(texture->get_uid());
    texture.reset();
    std::filesystem::remove(source_path);
    texture = assets->load_asset<Texture>(asset_path);
    REQUIRE(texture);
    REQUIRE(texture->texture_);
    CHECK(texture->texture_->get_info().format == FORMAT_BC1_SRGB);
    CHECK(texture->texture_->get_info().mip_levels == 3);
    auto backend = EngineContext::rhi();
    auto readback = backend->create_buffer({.size = 24, .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(readback);
    auto command = backend->get_immediate_command();
    for (uint32_t mip = 0; mip < 3; ++mip) {
        command->copy_texture_to_buffer(texture->texture_, {TEXTURE_ASPECT_COLOR, mip, 0, 1}, readback, mip * 8);
    }
    command->flush();
    auto* gpu_blocks = static_cast<const uint8_t*>(readback->map());
    REQUIRE(gpu_blocks);
    CHECK(std::memcmp(gpu_blocks, dds.data() + 128, 24) == 0);
    readback->unmap();
    assets->unload_asset(texture->get_uid());
    std::filesystem::remove(*stored_path);

    dds.resize(144);
    word(8, 0x81007); word(28, 0); word(108, 0x1000);
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
    auto dir = test_utils::TestContext::project_dir() / "assets/import_transform";
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
    CHECK(face_normal.dot(first->get_normals()[indices[0]]) < -0.99f);
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
    test_utils::TestContext::reset();
}

TEST_CASE("Scene import saves the winding for each instance", "[render_resource][scene_import]") {
    test_utils::TestContext::reset();
    const auto dir = test_utils::TestContext::project_dir() / "assets/import_scene_nodes";
    std::filesystem::create_directories(dir);
    const auto source = dir / "triangle.dae";
    {
        std::ofstream file(source);
        file << R"(<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset><unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>
<library_geometries><geometry id="triangle"><mesh>
  <source id="positions"><float_array id="coords" count="9">0 0 0 1 0 0 0 1 0</float_array>
    <technique_common><accessor source="#coords" count="3" stride="3">
      <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
    </accessor></technique_common>
  </source>
  <vertices id="vertices"><input semantic="POSITION" source="#positions"/></vertices>
  <triangles count="1"><input semantic="VERTEX" source="#vertices" offset="0"/><p>0 1 2</p></triangles>
</mesh></geometry></library_geometries>
<library_visual_scenes><visual_scene id="test">
  <node id="group"><translate>10 0 0</translate><rotate>0 0 1 90</rotate><scale>-1 1 1</scale>
    <node id="mirrored"><translate>0 5 0</translate><scale>2 3 1</scale>
      <instance_geometry url="#triangle"/>
    </node>
    <node id="mirrored_copy"><instance_geometry url="#triangle"/></node>
  </node>
  <node id="plain"><translate>-4 0 0</translate><instance_geometry url="#triangle"/></node>
</visual_scene></library_visual_scenes>
<scene><instance_visual_scene url="#test"/></scene></COLLADA>)";
    }
    ModelProcessSetting settings;
    ModelImporter importer;
    auto* assets = EngineContext::asset();
    const std::string scene_path = "/Game/import_scene_nodes/scene.asset";
    auto scene = importer.import_scene(source.string(), scene_path, settings);
    REQUIRE(scene);
    auto renderers = scene->get_components<MeshRendererComponent>();
    REQUIRE(renderers.size() == 3);
    REQUIRE(renderers[0]->get_model() == renderers[1]->get_model());
    REQUIRE(renderers[0]->get_model() != renderers[2]->get_model());
    const auto reversed_model = renderers[0]->get_model();
    const auto plain_model = renderers[2]->get_model();
    const auto reversed_mesh = reversed_model->get_mesh(0);
    const auto plain_mesh = plain_model->get_mesh(0);
    CHECK(reversed_mesh->get_positions()[0].x == Catch::Approx(0));
    const std::vector<uint32_t> plain_indices{2, 1, 0};
    const std::vector<uint32_t> reversed_indices{0, 1, 2};
    CHECK(plain_mesh->get_indices() == plain_indices);
    CHECK(reversed_mesh->get_indices() == reversed_indices);
    assets->save_asset(scene, scene_path);
    assets->unload_asset(scene->get_uid());
    assets->unload_asset(reversed_model->get_uid());
    assets->unload_asset(plain_model->get_uid());
    assets->unload_asset(reversed_mesh->get_uid());
    assets->unload_asset(plain_mesh->get_uid());
    scene.reset();
    std::filesystem::remove(source);
    scene = assets->load_asset<Scene>(scene_path);
    REQUIRE(scene);
    renderers = scene->get_components<MeshRendererComponent>();
    REQUIRE(renderers.size() == 3);
    CHECK(renderers[0]->get_model() != reversed_model);
    CHECK(renderers[0]->get_model() == renderers[1]->get_model());
    CHECK(renderers[0]->get_model()->get_mesh(0)->get_indices() == reversed_indices);
    CHECK(renderers[2]->get_model()->get_mesh(0)->get_indices() == plain_indices);
    std::vector<render::DrawBatch> batches;
    for (auto* renderer : renderers) renderer->collect_draw_batch(batches, nullptr);
    REQUIRE(batches.size() == 3);
    CHECK(batches[0].model_matrix.m[3][0] == Catch::Approx(5).margin(0.0001));
    CHECK(batches[2].model_matrix.m[3][0] == Catch::Approx(-4));
    std::filesystem::remove(dir / "scene.asset");
    test_utils::TestContext::reset();
}
