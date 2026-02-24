#include <catch2/catch_test_macros.hpp>
#include "engine/function/render/graph/rdg_builder.h"
#include <fstream>

TEST_CASE("RDG Graphviz Export", "[rdg]") {
    RDGBuilder builder;

    auto tex_color = builder.create_texture("ColorTex")
        .format(FORMAT_R8G8B8A8_UNORM)
        .extent({1920, 1080, 1})
        .allow_render_target()
        .finish();

    auto tex_depth = builder.create_texture("DepthTex")
        .format(FORMAT_D32_SFLOAT)
        .extent({1920, 1080, 1})
        .allow_depth_stencil()
        .finish();

    auto buf_uniform = builder.create_buffer("UniformBuf")
        .size(1024)
        .allow_read()
        .finish();

    builder.create_render_pass("ForwardPass")
        .color(0, tex_color, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE)
        .depth_stencil(tex_depth, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE)
        .read(0, 0, 0, buf_uniform)
        .execute([](RDGPassContext context) {
            // No-op
        });

    auto tex_output = builder.create_texture("OutputTex")
        .format(FORMAT_R8G8B8A8_UNORM)
        .extent({1920, 1080, 1})
        .allow_read_write()
        .finish();

    builder.create_compute_pass("PostProcess")
        .read(0, 0, 0, tex_color)
        .read_write(0, 1, 0, tex_output)
        .execute([](RDGPassContext context) {
            // No-op
        });

    std::string export_path = "test_rdg.dot";
    builder.export_graphviz(export_path);

    // Verify file exists
    std::ifstream f(export_path);
    REQUIRE(f.good());
    f.close();
}
