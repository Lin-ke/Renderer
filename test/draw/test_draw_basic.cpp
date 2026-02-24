#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/core/window/window.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/npr_forward_pass.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/input/input.h"
#include "engine/core/math/math.h"
#include "engine/core/log/Log.h"
#include "engine/platform/dx11/platform_rhi.h"

#include <d3dcompiler.h>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

/**
 * @file test/draw/test_draw_basic.cpp
 * @brief Basic drawing tests including triangle, cube, bunny model, and NPR rendering.
 */

DEFINE_LOG_TAG(LogDrawBasic, "DrawBasic");

// Helper to compile shader (used by triangle and cube tests)
static std::vector<uint8_t> compile_shader(const std::string& source, const std::string& entry, const std::string& profile) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, nullptr, nullptr, entry.c_str(), profile.c_str(), D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0, &blob, &error_blob);
    
    if (FAILED(hr)) {
        if (error_blob) {
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

// ==================== Basic Triangle Test ====================

struct PerFrameData {
    Mat4 view;
    Mat4 proj;
    Vec3 cameraPos;
    float padding;
};

struct PerObjectData {
    Mat4 model;
};

struct PerLightData {
    Vec3 lightPos;
    float padding1;
    Vec3 lightColor;
    float lightIntensity;
};

TEST_CASE("Basic Drawing Tests", "[draw]") {
    SECTION("DX11 Swapchain and Fence - Basic Triangle") {
        // Create a hidden window for testing
        auto window = std::make_unique<Window>(800, 600, L"DX11 Test", false);
        REQUIRE(window->get_hwnd() != nullptr);

        // 1. Initialize Backend
        RHIBackendInfo info = {};
        info.type = BACKEND_DX11;
        info.enable_debug = true;
        RHIBackendRef backend = RHIBackend::init(info);
        REQUIRE(backend != nullptr);

        // 2. Create Surface using native HWND
        RHISurfaceRef surface = backend->create_surface(window->get_hwnd());
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
        uint32_t image_count = 0;
        while (swapchain->get_texture(image_count) != nullptr) {
            image_count++;
        }
        
        for (uint32_t i = 0; i < image_count; ++i) {
            RHITextureRef tex = swapchain->get_texture(i);
            RHITextureViewInfo view_info = {};
            view_info.texture = tex;
            RHITextureViewRef view = backend->create_texture_view(view_info);
            swapchain_views.push_back(view);
        }

        // 4. Prepare Resources for Triangle
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

        RHIShaderInfo vs_info = { "main", SHADER_FREQUENCY_VERTEX, compile_shader(vs_source, "main", "vs_5_0") };
        RHIShaderInfo ps_info = { "main", SHADER_FREQUENCY_FRAGMENT, compile_shader(ps_source, "main", "ps_5_0") };
        auto vs = backend->create_shader(vs_info);
        auto ps = backend->create_shader(ps_info);
        REQUIRE(vs != nullptr);
        REQUIRE(ps != nullptr);

        // Triangle Vertex Buffer (Position + Color)
        float vertices[] = {
            // Position              Color
             0.0f,  0.5f, 0.0f,     1.0f, 0.0f, 0.0f,  // Top - Red
             0.5f, -0.5f, 0.0f,     0.0f, 1.0f, 0.0f,  // Right - Green
            -0.5f, -0.5f, 0.0f,     0.0f, 0.0f, 1.0f,  // Left - Blue
        };

        RHIBufferInfo vb_info = { sizeof(vertices), 6 * sizeof(float), MEMORY_USAGE_CPU_TO_GPU, RESOURCE_TYPE_VERTEX_BUFFER };
        auto vb = backend->create_buffer(vb_info);
        void* vb_ptr = vb->map(); memcpy(vb_ptr, vertices, sizeof(vertices)); vb->unmap();

        // Pipeline
        RHIGraphicsPipelineInfo pipe_info = {};
        pipe_info.vertex_shader = vs;
        pipe_info.fragment_shader = ps;
        pipe_info.vertex_input_state.vertex_elements = {
            { 0, 0, FORMAT_R32G32B32_SFLOAT, 0, 6 * sizeof(float), false, {0}, "POSITION", 0 },
            { 0, 1, FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float), 6 * sizeof(float), false, {0}, "NORMAL", 0 }
        };
        pipe_info.depth_stencil_state.enable_depth_test = false;
        auto pipeline = backend->create_graphics_pipeline(pipe_info);
        REQUIRE(pipeline != nullptr);

        // 5. Render Loop
        const int MAX_FRAMES_IN_FLIGHT = 2;
        std::array<RHIFenceRef, MAX_FRAMES_IN_FLIGHT> flight_fences;
        std::array<bool, MAX_FRAMES_IN_FLIGHT> flight_fence_active = { false, false };
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            flight_fences[i] = backend->create_fence(false);
        }
        int current_frame = 0;

        RHICommandPoolInfo pool_info = {};
        RHICommandPoolRef pool = backend->create_command_pool(pool_info);
        RHICommandContextRef context = backend->create_command_context(pool);

        int frame_count = 0;

        while (frame_count < 300) { 
            if (!window->process_messages()) {
                break;
            }
            frame_count++;
            
            if (flight_fence_active[current_frame]) {
                flight_fences[current_frame]->wait();
            }

            RHITextureRef back_buffer = swapchain->get_new_frame(nullptr, nullptr);
            uint32_t image_index = swapchain->get_current_frame_index();
            RHITextureViewRef back_buffer_view = swapchain_views[image_index];

            RHIRenderPassInfo rp_info = {};
            rp_info.color_attachments[0].texture_view = back_buffer_view;
            rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
            rp_info.color_attachments[0].clear_color = { 0.1f, 0.2f, 0.4f, 1.0f };
            
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
            
            context->execute(flight_fences[current_frame], nullptr, nullptr);
            flight_fence_active[current_frame] = true;

            swapchain->present(nullptr);

            current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

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
        
        window.reset();
    }

    SECTION("Draw Cube Blinn-Phong") {
        test_utils::TestContext::reset();

        auto* rhi = EngineContext::render_system()->get_rhi().get();
        void* window_handle = EngineContext::render_system()->get_window_handle();
        REQUIRE(window_handle != nullptr);
        
        // 2. Setup Scene
        auto scene = std::make_shared<Scene>();

        // Camera
        auto camera_ent = scene->create_entity();
        auto cam_trans = camera_ent->add_component<TransformComponent>();
        cam_trans->transform.set_position({0.0f, 2.0f, 5.0f});
        cam_trans->transform.set_rotation(Vec3(-20.0f, 0.0f, 0.0f));
        auto cam_comp = camera_ent->add_component<CameraComponent>();
        cam_comp->set_fov(60.0f);
        cam_comp->on_init();

        // Light
        auto light_ent = scene->create_entity();
        auto light_trans = light_ent->add_component<TransformComponent>();
        light_trans->transform.set_position({5.0f, 5.0f, 0.0f});
        auto light_comp = light_ent->add_component<PointLightComponent>();
        light_comp->set_color({1.0f, 1.0f, 1.0f});
        light_comp->set_intensity(1.5f);
        light_comp->on_init();

        // Cube
        auto cube_ent = scene->create_entity();
        auto cube_trans = cube_ent->add_component<TransformComponent>();
        cube_trans->transform.set_position({0.0f, 0.0f, 0.0f});
        auto cube_mesh = cube_ent->add_component<MeshRendererComponent>();
        cube_mesh->on_init();

        // 3. RHI Resources
        const std::string vs_source = R"(
            cbuffer PerFrame : register(b0) { float4x4 view; float4x4 proj; float3 cameraPos; };
            cbuffer PerObject : register(b1) { float4x4 model; };
            struct VSInput { float3 position : POSITION; float3 normal : NORMAL; };
            struct PSInput { float4 position : SV_POSITION; float3 worldPos : POSITION; float3 normal : NORMAL; };
            PSInput main(VSInput input) {
                PSInput output;
                float4 worldPos = mul(model, float4(input.position, 1.0));
                output.worldPos = worldPos.xyz;
                output.position = mul(proj, mul(view, worldPos));
                output.normal = mul((float3x3)model, input.normal);
                return output;
            }
        )";
        const std::string ps_source = R"(
            cbuffer PerLight : register(b2) { float3 lightPos; float3 lightColor; float lightIntensity; };
            struct PSInput { float4 position : SV_POSITION; float3 worldPos : POSITION; float3 normal : NORMAL; };
            float4 main(PSInput input) : SV_TARGET {
                float3 N = normalize(input.normal);
                float3 L = normalize(lightPos - input.worldPos);
                float3 V = normalize(float3(0, 2, 5) - input.worldPos);
                float3 H = normalize(L + V);
                float3 ambient = float3(0.1, 0.1, 0.1) * lightColor;
                float diff = max(dot(N, L), 0.0);
                float3 diffuse = diff * lightColor * lightIntensity;
                float spec = pow(max(dot(N, H), 0.0), 32.0);
                float3 specular = spec * lightColor * lightIntensity;
                return float4(ambient + diffuse + specular, 1.0);
            }
        )";

        RHIShaderInfo vs_info = { "main", SHADER_FREQUENCY_VERTEX, compile_shader(vs_source, "main", "vs_5_0") };
        RHIShaderInfo ps_info = { "main", SHADER_FREQUENCY_FRAGMENT, compile_shader(ps_source, "main", "ps_5_0") };
        auto vs = rhi->create_shader(vs_info);
        auto ps = rhi->create_shader(ps_info);

        // Cube Geometry (Pos, Normal)
        float vertices[] = {
            // Front face
            -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
             0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
             0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
            // Back face
            -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f,
        };
        uint32_t indices[] = { 0, 1, 2, 2, 3, 0,  4, 5, 6, 6, 7, 4 };

        RHIBufferInfo vb_info = { sizeof(vertices), 6 * sizeof(float), MEMORY_USAGE_CPU_TO_GPU, RESOURCE_TYPE_VERTEX_BUFFER };
        auto vb = rhi->create_buffer(vb_info);
        void* vb_ptr = vb->map(); memcpy(vb_ptr, vertices, sizeof(vertices)); vb->unmap();

        RHIBufferInfo ib_info = { sizeof(indices), 4, MEMORY_USAGE_CPU_TO_GPU, RESOURCE_TYPE_INDEX_BUFFER };
        auto ib = rhi->create_buffer(ib_info);
        void* ib_ptr = ib->map(); memcpy(ib_ptr, indices, sizeof(indices)); ib->unmap();

        // Uniform Buffers
        auto ub_frame = rhi->create_buffer({ sizeof(PerFrameData), 0, MEMORY_USAGE_CPU_TO_GPU, RESOURCE_TYPE_UNIFORM_BUFFER });
        auto ub_object = rhi->create_buffer({ sizeof(PerObjectData), 0, MEMORY_USAGE_CPU_TO_GPU, RESOURCE_TYPE_UNIFORM_BUFFER });
        auto ub_light = rhi->create_buffer({ sizeof(PerLightData), 0, MEMORY_USAGE_CPU_TO_GPU, RESOURCE_TYPE_UNIFORM_BUFFER });

        // Pipeline
        RHIGraphicsPipelineInfo pipe_info = {};
        pipe_info.vertex_shader = vs;
        pipe_info.fragment_shader = ps;
        pipe_info.vertex_input_state.vertex_elements = {
            { 0, 0, FORMAT_R32G32B32_SFLOAT, 0, 6 * sizeof(float), false, {0}, "POSITION", 0 },
            { 0, 1, FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float), 6 * sizeof(float), false, {0}, "NORMAL", 0 }
        };
        pipe_info.depth_stencil_state.enable_depth_test = false;
        auto pipeline = rhi->create_graphics_pipeline(pipe_info);

        // Frame Resources
        auto swapchain = EngineContext::render_system()->get_swapchain();
        RHICommandPoolRef pool = rhi->create_command_pool({});
        RHICommandContextRef cmd = rhi->create_command_context(pool);
        RHIFenceRef fence = rhi->create_fence(false);

        // 4. Loop
        int frames = 0;
        while (frames < 1) {
            cam_comp->on_update(0.016f);
            light_comp->on_update(0.016f);
            cube_mesh->on_update(0.016f);

            PerFrameData frame_data = { cam_comp->get_view_matrix(), cam_comp->get_projection_matrix(), cam_comp->get_position(), 0.0f };
            void* ptr = ub_frame->map(); memcpy(ptr, &frame_data, sizeof(frame_data)); ub_frame->unmap();

            PerObjectData obj_data = { cube_trans->transform.get_matrix() };
            ptr = ub_object->map(); memcpy(ptr, &obj_data, sizeof(obj_data)); ub_object->unmap();

            PerLightData light_data = { light_trans->transform.get_position(), 0.0f, light_comp->get_color(), light_comp->get_intensity() }; 
            light_data.lightColor = {1.0f, 1.0f, 1.0f};
            light_data.lightIntensity = 1.5f;
            ptr = ub_light->map(); memcpy(ptr, &light_data, sizeof(light_data)); ub_light->unmap();

            auto back_buffer = swapchain->get_new_frame(nullptr, nullptr);
            RHIRenderPassInfo rp_info = {};
            rp_info.color_attachments[0].texture_view = rhi->create_texture_view({back_buffer});
            rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
            rp_info.color_attachments[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            
            auto pass = rhi->create_render_pass(rp_info);

            cmd->begin_command();
            cmd->begin_render_pass(pass);
            cmd->set_graphics_pipeline(pipeline);
            cmd->set_viewport({0, 0}, {800, 600});
            cmd->set_scissor({0, 0}, {800, 600});
            cmd->bind_vertex_buffer(vb, 0, 0);
            cmd->bind_index_buffer(ib, 0);
            
            cmd->bind_constant_buffer(ub_frame, 0, SHADER_FREQUENCY_VERTEX);
            cmd->bind_constant_buffer(ub_object, 1, SHADER_FREQUENCY_VERTEX);
            cmd->bind_constant_buffer(ub_light, 2, SHADER_FREQUENCY_FRAGMENT);

            cmd->draw_indexed(12, 1, 0, 0, 0); 
            
            cmd->end_render_pass();
            cmd->end_command();
            cmd->execute(fence, nullptr, nullptr);
            fence->wait();
            
            swapchain->present(nullptr);
            
            pass->destroy();
            rp_info.color_attachments[0].texture_view->destroy();
            
            frames++;
        }
        
        test_utils::TestContext::reset();
    }
}

// ==================== Bunny Model Tests ====================

static const std::string BUNNY_SCENE_PATH = "/Game/bunny_scene.asset";
static const std::string BUNNY_MODEL_PATH = "/Engine/models/bunny.obj";

static bool create_and_save_bunny_scene(const std::string& scene_path) {
    INFO(LogDrawBasic, "=== Creating Bunny Scene ===");
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({-3.0f, 0.0f, 0.0f});
    
    auto* camera = camera_ent->add_component<CameraComponent>();
    camera->set_fov(60.0f);
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 1.0f, 1.0f});
    light->set_intensity(1.5f);
    light->set_enable(true);
    
    // Create bunny entity
    auto* bunny_ent = scene->create_entity();
    auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
    bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
    
    // Load bunny model
    INFO(LogDrawBasic, "Loading bunny model from: {}", BUNNY_MODEL_PATH);
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;
    setting.flip_uv = false;
    setting.material_type = ModelMaterialType::PBR;
    
    auto bunny_model = Model::Load(BUNNY_MODEL_PATH, setting);
    if (!bunny_model || bunny_model->get_submesh_count() == 0) {
        ERR(LogDrawBasic, "Failed to load bunny model");
        return false;
    }
    
    INFO(LogDrawBasic, "Bunny model loaded: {} submeshes", bunny_model->get_submesh_count());
    
    // Add MeshRendererComponent
    auto* bunny_renderer = bunny_ent->add_component<MeshRendererComponent>();
    bunny_renderer->set_model(bunny_model);
    
    // Create and assign PBR material
    auto bunny_mat = std::make_shared<PBRMaterial>();
    bunny_mat->set_diffuse({0.8f, 0.5f, 0.3f, 1.0f});
    bunny_mat->set_roughness(0.2f);
    bunny_mat->set_metallic(0.8f);
    bunny_renderer->set_material(bunny_mat);
    
    // Save scene
    INFO(LogDrawBasic, "Saving scene to: {}", scene_path);
    auto am = EngineContext::asset();
    if (!am) {
        ERR(LogDrawBasic, "AssetManager is null");
        return false;
    }
    
    am->save_asset(scene, scene_path);
    
    auto saved_scene = am->get_asset_immediate(scene->get_uid());
    if (!saved_scene) {
        ERR(LogDrawBasic, "Failed to verify saved scene");
        return false;
    }
    
    INFO(LogDrawBasic, "Scene saved successfully, UID: {}", scene->get_uid().to_string());
    return true;
}

TEST_CASE("Bunny Model Tests", "[draw]") {
    SECTION("Render Bunny Model") {
        test_utils::TestContext::reset();
        
        std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
        
        REQUIRE(EngineContext::rhi() != nullptr);
        REQUIRE(EngineContext::render_system() != nullptr);
        REQUIRE(EngineContext::world() != nullptr);
        
        test_utils::RenderTestApp::Config config;
        config.scene_path = BUNNY_SCENE_PATH;
        config.width = 1280;
        config.height = 720;
        config.max_frames = 60;
        config.capture_frame = 30;
        config.create_scene_func = create_and_save_bunny_scene;
        
        std::vector<uint8_t> screenshot_data;
        int frames = 0;
        bool screenshot_taken = test_utils::RenderTestApp::run(config, screenshot_data, &frames);
        
        CHECK(frames > 0);
        
        // Save screenshot
        if (screenshot_taken) {
            std::string screenshot_path = test_asset_dir + "/bunny_screenshot.png";
            if (test_utils::save_screenshot_png(screenshot_path, config.width, config.height, screenshot_data)) {
                float brightness = test_utils::calculate_average_brightness(screenshot_data);
                INFO(LogDrawBasic, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
                CHECK(brightness > 1.0f);
            }
        }
        
        test_utils::TestContext::reset();
    }

    SECTION("Camera Movement") {
        test_utils::TestContext::reset();
        
        auto scene = std::make_shared<Scene>();
        
        auto* camera_ent = scene->create_entity();
        auto* cam_trans = camera_ent->add_component<TransformComponent>();
        cam_trans->transform.set_position({0.0f, 0.0f, 5.0f});
        
        auto* cam_comp = camera_ent->add_component<CameraComponent>();
        cam_comp->on_init();
        
        Vec3 initial_pos = cam_trans->transform.get_position();
        cam_comp->on_update(16.0f);
        Vec3 final_pos = cam_trans->transform.get_position();
        
        REQUIRE(cam_comp->get_position() == final_pos);
        
        test_utils::TestContext::reset();
    }
}

// ==================== NPR Klee Test ====================

static const std::string PBR_MODEL_PATH = "/Engine/models/Klee/klee.fbx";
static const std::string NPR_SCENE_SAVE_PATH = "/Game/npr_klee_test.asset";

static bool create_and_save_npr_scene(const std::string& scene_path) {
    INFO(LogDrawBasic, "=== Creating NPR Scene ===");
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({-30.0f, 10.0f, 0.0f});
    cam_trans->transform.set_rotation({0.0f, -15.0f, 0.0f});
    
    auto* camera = camera_ent->add_component<CameraComponent>();
    camera->set_fov(60.0f);
    camera->set_far(1000.0f);
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({100.0f, 200.0f, 100.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 1.0f, 1.0f});
    light->set_intensity(100.0f);
    light->set_enable(true);
    
    // Create model entity
    auto* model_ent = scene->create_entity();
    auto* model_trans = model_ent->add_component<TransformComponent>();
    model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    model_trans->transform.set_scale({1.0f, 1.0f, 1.0f});
    
    // Load NPR model
    INFO(LogDrawBasic, "Loading NPR model from: {}", PBR_MODEL_PATH);
    
    ModelProcessSetting npr_setting;
    npr_setting.smooth_normal = true;
    npr_setting.load_materials = true;
    npr_setting.flip_uv = true;
    npr_setting.material_type = ModelMaterialType::NPR;
    
    auto npr_model = Model::Load(PBR_MODEL_PATH, npr_setting);
    if (npr_model->get_submesh_count() == 0) return false;
    
    INFO(LogDrawBasic, "NPR model loaded: {} submeshes", npr_model->get_submesh_count());
    
    // Add MeshRendererComponent
    auto* model_mesh = model_ent->add_component<MeshRendererComponent>();
    model_mesh->set_model(npr_model);
    
    // Auto-adjust camera to model bounding box
    auto box = npr_model->get_bounding_box();
    Vec3 center = (box.min + box.max) * 0.5f;
    float size = (box.max - box.min).norm();
    
    float dist = size * 1.5f;
    if (dist < 1.0f) dist = 5.0f;
    
    cam_trans->transform.set_position(center + Vec3(-dist, size * 0.5f, 0.0f));
    
    INFO(LogDrawBasic, "Model bounds: min=({},{},{}), max=({},{},{}), size={}",
         box.min.x(), box.min.y(), box.min.z(), 
         box.max.x(), box.max.y(), box.max.z(), size);
    
    // Save scene
    INFO(LogDrawBasic, "Saving scene to: {}", scene_path);
    auto am = EngineContext::asset();
    if (!am) {
        ERR(LogDrawBasic, "AssetManager is null");
        return false;
    }
    
    am->save_asset(scene, scene_path);
    
    auto saved_scene = am->get_asset_immediate(scene->get_uid());
    if (!saved_scene) {
        ERR(LogDrawBasic, "Failed to verify saved scene");
        return false;
    }
    
    INFO(LogDrawBasic, "Scene saved successfully, UID: {}", scene->get_uid().to_string());
    return true;
}

TEST_CASE("NPR Model Rendering", "[draw][npr]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = NPR_SCENE_SAVE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 60;
    config.capture_frame = 45;
    config.create_scene_func = create_and_save_npr_scene;
    config.on_scene_loaded_func = [](test_utils::SceneLoadResult& result) {
        if (auto mesh_manager = EngineContext::render_system()->get_mesh_manager()) {
            mesh_manager->set_npr_enabled(true);
            mesh_manager->set_active_camera(result.camera);
        }
    };

    std::vector<uint8_t> screenshot_data;
    int frames = 0;
    bool screenshot_taken = test_utils::RenderTestApp::run(config, screenshot_data, &frames);
    
    CHECK(frames > 0);
    
    // Save screenshot
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/klee_npr_screenshot.png";
        if (test_utils::save_screenshot_png(screenshot_path, config.width, config.height, screenshot_data)) {
            float brightness = test_utils::calculate_average_brightness(screenshot_data);
            INFO(LogDrawBasic, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
            CHECK(brightness > 0.0f);
        }
    }
    
    test_utils::TestContext::reset();
}
