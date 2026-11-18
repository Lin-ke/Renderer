#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/render_resource/mesh.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>

TEST_CASE("Native mesh archives reload compact attributes into correct GPU buffers", "[render_resource][asset_load]") {
    test_utils::TestContext::reset();
    const std::vector<Vec3> positions{{947.584961f, 416, -189.129379f},
        {637.587341f, 416, -726.066467f}, {637.587341f, 1036, -726.066467f}};
    const std::vector<Vec3> normals{{-0.5f, 0.125f, 1}, {-1, 0, 0.5f}, {0.25f, -0.5f, 1}};
    const std::vector<Vec4> tangents{{0.5f, -0.5f, 0.25f, -1}, {1, 0, 0, 1}, {-1, 0.5f, 0.125f, 1}};
    const std::vector<Vec2> uvs{{-0.125f, 2.5f}, {0, 1}, {0.5f, 0.25f}};
    const std::vector<uint32_t> indices{0, 1, 2};
    std::vector<uint8_t> expected;
    auto append = [&](const auto& values) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
        expected.insert(expected.end(), bytes, bytes + values.size() * sizeof(values[0]));
    };
    append(positions); append(normals); append(tangents); append(uvs); append(indices);
    auto* assets = EngineContext::asset();
    auto backend = EngineContext::rhi();
    for (const auto* extension : {".asset", ".binasset"}) {
        const std::string path = std::string("/Game/compact_mesh/triangle") + extension;
        auto mesh = Mesh::Create(positions, normals, tangents, uvs, indices, "CompactMesh");
        assets->save_asset(mesh, path);
        const auto uid = mesh->get_uid();
        assets->unload_asset(uid);
        mesh.reset();
        const std::string original = "947.584961 416.000000 -189.129379";
        if (std::string_view(extension) == ".asset") {
            // Human-edited legacy JSON can contain signs, exponents and whitespace.
            const auto physical = assets->get_physical_path(path);
            REQUIRE(physical);
            std::string encoded;
            { std::ifstream file(*physical); encoded.assign(std::istreambuf_iterator<char>(file), {}); }
            const auto offset = encoded.find(original);
            REQUIRE(offset != std::string::npos);
            encoded.replace(offset, original.size(), "+9.47584961e2\\t4.16e2  -1.89129379e2");
            { std::ofstream file(*physical); file << encoded; }
        } else {
            // Exercise long legacy strings beyond the decoder's stack buffer.
            const auto physical = assets->get_physical_path(path);
            REQUIRE(physical);
            std::string encoded;
            { std::ifstream file(*physical, std::ios::binary); encoded.assign(std::istreambuf_iterator<char>(file), {}); }
            const auto offset = encoded.find(original);
            REQUIRE(offset != std::string::npos);
            const auto padded = std::string(300, ' ') + original;
            const uint64_t size = padded.size();
            encoded.replace(offset, original.size(), padded);
            std::memcpy(encoded.data() + offset - sizeof(size), &size, sizeof(size));
            { std::ofstream file(*physical, std::ios::binary); file.write(encoded.data(), encoded.size()); }
        }
        auto loaded = assets->load_asset<Mesh>(uid);
        REQUIRE(loaded);
        REQUIRE(loaded->get_vertex_buffer());
        REQUIRE(loaded->get_index_buffer());
        const auto vertex = loaded->get_vertex_buffer();
        const std::array sources{vertex->position_buffer_, vertex->normal_buffer_, vertex->tangent_buffer_,
            vertex->tex_coord_buffer_, loaded->get_index_buffer()->buffer_};
        REQUIRE(std::all_of(sources.begin(), sources.end(), [](const auto& buffer) { return bool(buffer); }));
        auto readback = backend->create_buffer({.size = expected.size(), .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
        REQUIRE(readback);
        const auto command = backend->get_immediate_command();
        size_t offset = 0;
        for (const auto& source : sources) {
            command->copy_buffer(source, 0, readback, offset, source->get_info().size);
            offset += source->get_info().size;
        }
        command->flush();
        auto* result = readback->map();
        REQUIRE(result);
        CHECK(offset == expected.size());
        CHECK(std::memcmp(result, expected.data(), expected.size()) == 0);
        readback->unmap();
    }
    CHECK_FALSE(backend->check_debug_messages("compact native mesh loading"));
    test_utils::TestContext::reset();
}
