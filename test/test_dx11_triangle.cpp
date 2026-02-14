#include <catch2/catch_test_macros.hpp>
#include <GLFW/glfw3.h>
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/main/engine_context.h"
#include "engine/platform/dx11/platform_rhi.h"
#include <d3dcompiler.h>

// Helper to compile shader
std::vector<uint8_t> compile_shader(const std::string& source, const std::string& entry, const std::string& profile) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, nullptr, nullptr, entry.c_str(), profile.c_str(), D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0, &blob, &error_blob);
    
    if (FAILED(hr)) {
        if (error_blob) {
            // In a real engine, use the Log system. Here we just print to debug output or stderr
            OutputDebugStringA((char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return {};
    }
    
    std::vector<uint8_t> code(blob->GetBufferSize());
    memcpy(code.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release();
    if (error_blob) error_blob->Release();
    return code;
}

TEST_CASE("DX11 Swapchain and Fence", "[dx11_triangle]") {
    // Initialize GLFW
    REQUIRE(glfwInit() == GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "DX11 Test", nullptr, nullptr);
    REQUIRE(window != nullptr);

    // 1. Initialize Backend
    RHIBackendInfo info = {};
    info.type = BACKEND_DX11;
    info.enable_debug = true;
    RHIBackendRef backend = RHIBackend::init(info);
    REQUIRE(backend != nullptr);

    // 2. Create Surface
    RHISurfaceRef surface = backend->create_surface(window);
    REQUIRE(surface != nullptr);

    // 3. Create Swapchain
    RHISwapchainInfo sw_info = {};
    sw_info.surface = surface;
    sw_info.image_count = 2;
    sw_info.extent = { 800, 600 };
    sw_info.format = FORMAT_R8G8B8A8_UNORM;
    RHISwapchainRef swapchain = backend->create_swapchain(sw_info);
    REQUIRE(swapchain != nullptr);

    // Pre-create texture views for all swapchain images
    std::vector<RHITextureViewRef> swapchain_views;
    for (uint32_t i = 0; i < sw_info.image_count; ++i) {
        RHITextureViewInfo view_info = {};
        view_info.texture = swapchain->get_texture(i);
        swapchain_views.push_back(backend->create_texture_view(view_info));
    }

    // 4. Prepare Resources for Triangle
    // Shaders
    const std::string vs_source = R"(
        struct VSInput {
            float3 position : POSITION0;
            float3 color : POSITION1;
        };
        struct VSOutput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = float4(input.position, 1.0);
            output.color = float4(input.color, 1.0);
            return output;
        }
    )";

    const std::string ps_source = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };
        float4 main(PSInput input) : SV_TARGET {
            return input.color;
        }
    )";

    RHIShaderInfo vs_info = {};
    vs_info.entry = "main";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = compile_shader(vs_source, "main", "vs_5_0");
    REQUIRE(!vs_info.code.empty());
    RHIShaderRef vs = backend->create_shader(vs_info);

    RHIShaderInfo ps_info = {};
    ps_info.entry = "main";
    ps_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    ps_info.code = compile_shader(ps_source, "main", "ps_5_0");
    REQUIRE(!ps_info.code.empty());
    RHIShaderRef ps = backend->create_shader(ps_info);

    // Vertex Buffer
    float vertices[] = {
        // Position         // Color
         0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f
    };

    RHIBufferInfo vb_info = {};
    vb_info.size = sizeof(vertices);
    vb_info.stride = 6 * sizeof(float);
    vb_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU; // Simple upload
    vb_info.type = RESOURCE_TYPE_VERTEX_BUFFER;
    RHIBufferRef vb = backend->create_buffer(vb_info);
    REQUIRE(vb != nullptr);
    
    void* data = vb->map();
    REQUIRE(data != nullptr);
    memcpy(data, vertices, sizeof(vertices));
    vb->unmap();

    // Graphics Pipeline
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vs;
    pipe_info.fragment_shader = ps;
    
    pipe_info.vertex_input_state.vertex_elements.resize(2);
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].attribute_index = 0; // POSITION0
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[1].attribute_index = 1; // POSITION1
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 3 * sizeof(float);

    pipe_info.depth_stencil_state.enable_depth_test = false;

    RHIGraphicsPipelineRef pipeline = backend->create_graphics_pipeline(pipe_info);
    REQUIRE(pipeline != nullptr);

    // 5. Render Loop
    const int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t current_frame = 0;
    std::vector<RHIFenceRef> flight_fences;
    std::vector<bool> flight_fence_active; // Track if the fence has been submitted at least once

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        flight_fences.push_back(backend->create_fence(false));
        flight_fence_active.push_back(false);
    }

    RHICommandPoolInfo pool_info = {};
    RHICommandPoolRef pool = backend->create_command_pool(pool_info);
    RHICommandContextRef context = backend->create_command_context(pool);

    // Get a frame and present (Loop)
    int frame_count = 0;

    while (!glfwWindowShouldClose(window) && frame_count < 300) { 
        glfwPollEvents();
        frame_count++;
        // Wait for the previous frame in this slot to finish
        if (flight_fence_active[current_frame]) {
            flight_fences[current_frame]->wait();
        }

        // Basic Frame Logic
        RHITextureRef back_buffer = swapchain->get_new_frame(nullptr, nullptr);
        
        // Note: For frames in flight, we usually use the swapchain image index if we have per-image resources.
        // But here we only have per-frame-in-flight resources (fences).
        // The swapchain might return image 0, then 1, then 0... matching our frames in flight, but not guaranteed.
        uint32_t image_index = swapchain->get_current_frame_index();
        RHITextureViewRef back_buffer_view = swapchain_views[image_index];

        // Define a simple clear pass
        RHIRenderPassInfo rp_info = {};
        rp_info.color_attachments[0].texture_view = back_buffer_view;
        rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
        rp_info.color_attachments[0].clear_color = { 0.1f, 0.2f, 0.4f, 1.0f }; // Cornflower Blue
        
        RHIRenderPassRef render_pass = backend->create_render_pass(rp_info);

        context->begin_command();
        context->begin_render_pass(render_pass);
        
        context->set_graphics_pipeline(pipeline);
        context->set_viewport({0, 0}, {800, 600});
        context->set_scissor({0, 0}, {800, 600});
        context->bind_vertex_buffer(vb, 0, 0);
        context->draw(3, 1, 0, 0);

        context->end_render_pass();
        context->end_command();
        
        // Submit and signal the fence for this frame slot
        context->execute(flight_fences[current_frame], nullptr, nullptr);
        flight_fence_active[current_frame] = true;

        swapchain->present(nullptr);

        // Advance to next frame slot
        current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

        // Simple cleanup for this test loop
        render_pass->destroy();
    }

    // Wait for all frames to complete before cleanup
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (flight_fence_active[i]) {
            flight_fences[i]->wait();
        }
    }

    // Cleanup
    pipeline->destroy();
    vb->destroy();
    vs->destroy();
    ps->destroy();

    for (auto view : swapchain_views) view->destroy();
    context->destroy();
    pool->destroy();
    swapchain->destroy();
    backend->destroy();
    glfwDestroyWindow(window);
    // Note: glfwTerminate() is not called here because other tests may use GLFW
    // The OS will clean up resources when the process exits
}
