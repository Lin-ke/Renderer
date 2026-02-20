#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#define MAX_QUEUE_CNT 2
#define MAX_RENDER_TARGETS 8
#define MAX_SHADER_IN_OUT_VARIABLES 8
#define MAX_DESCRIPTOR_SETS 8

using RHICommandContextImmediateRef = std::shared_ptr<class RHICommandContextImmediate>;
using RHICommandContextRef = std::shared_ptr<class RHICommandContext>;
using RHIBackendRef = std::shared_ptr<class RHIBackend>;
using RHIResourceRef = std::shared_ptr<class RHIResource>;
using RHIBufferRef = std::shared_ptr<class RHIBuffer>;
using RHITextureRef = std::shared_ptr<class RHITexture>;
using RHITextureViewRef = std::shared_ptr<class RHITextureView>;
using RHISamplerRef = std::shared_ptr<class RHISampler>;
using RHIShaderRef = std::shared_ptr<class RHIShader>;
using RHIShaderBindingTableRef = std::shared_ptr<class RHIShaderBindingTable>;
using RHITopLevelAccelerationStructureRef = std::shared_ptr<class RHITopLevelAccelerationStructure>;
using RHIBottomLevelAccelerationStructureRef = std::shared_ptr<class RHIBottomLevelAccelerationStructure>;
using RHIRootSignatureRef = std::shared_ptr<class RHIRootSignature>;
using RHIDescriptorSetRef = std::shared_ptr<class RHIDescriptorSet>;
using RHIRenderPassRef = std::shared_ptr<class RHIRenderPass>;
using RHIGraphicsPipelineRef = std::shared_ptr<class RHIGraphicsPipeline>;
using RHIComputePipelineRef = std::shared_ptr<class RHIComputePipeline>;
using RHIRayTracingPipelineRef = std::shared_ptr<class RHIRayTracingPipeline>;
using RHIQueueRef = std::shared_ptr<class RHIQueue>;
using RHISurfaceRef = std::shared_ptr<class RHISurface>;
using RHISwapchainRef = std::shared_ptr<class RHISwapchain>;
using RHICommandPoolRef = std::shared_ptr<class RHICommandPool>;
using RHIFenceRef = std::shared_ptr<class RHIFence>;
using RHISemaphoreRef = std::shared_ptr<class RHISemaphore>;

enum RHIResourceType : uint32_t {
    RHI_BUFFER = 0,
    RHI_TEXTURE,
    RHI_TEXTURE_VIEW,
    RHI_SAMPLER,
    RHI_SHADER,
    RHI_SHADER_BINDING_TABLE,
    RHI_TOP_LEVEL_ACCELERATION_STRUCTURE,
    RHI_BOTTOM_LEVEL_ACCELERATION_STRUCTURE,

    RHI_ROOT_SIGNATURE,
    RHI_DESCRIPTOR_SET,

    RHI_RENDER_PASS,
    RHI_GRAPHICS_PIPELINE,
    RHI_COMPUTE_PIPELINE,
    RHI_RAY_TRACING_PIPELINE,

    RHI_QUEUE,
    RHI_SURFACE,
    RHI_SWAPCHAIN,
    RHI_COMMAND_POOL,
    RHI_COMMAND_CONTEXT,
    RHI_COMMAND_CONTEXT_IMMEDIATE,
    RHI_FENCE,
    RHI_SEMAPHORE,

    RHI_RESOURCE_TYPE_MAX_CNT,
};

enum QueueType : uint32_t {
    QUEUE_TYPE_GRAPHICS = 0,
    QUEUE_TYPE_COMPUTE,
    QUEUE_TYPE_TRANSFER,

    QUEUE_TYPE_MAX_ENUM,
};

enum MemoryUsage : uint32_t {
    MEMORY_USAGE_UNKNOWN = 0,
    MEMORY_USAGE_GPU_ONLY = 1,
    MEMORY_USAGE_CPU_ONLY = 2,
    MEMORY_USAGE_CPU_TO_GPU = 3,
    MEMORY_USAGE_GPU_TO_CPU = 4,

    MEMORY_USAGE_MAX_ENUM = 0x7FFFFFFF,
};

enum ResourceTypeBits : uint32_t {
    RESOURCE_TYPE_NONE = 0x00000000,
    RESOURCE_TYPE_SAMPLER = 0x00000001,
    RESOURCE_TYPE_TEXTURE = 0x00000002,
    RESOURCE_TYPE_RW_TEXTURE = 0x00000004,
    RESOURCE_TYPE_TEXTURE_CUBE = 0x00000008,
    RESOURCE_TYPE_RENDER_TARGET = 0x00000010,
    RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER = 0x00000020,
    RESOURCE_TYPE_BUFFER = 0x00000040,
    RESOURCE_TYPE_RW_BUFFER = 0x00000080,
    RESOURCE_TYPE_UNIFORM_BUFFER = 0x00000100,
    RESOURCE_TYPE_VERTEX_BUFFER = 0x00000200,
    RESOURCE_TYPE_INDEX_BUFFER = 0x00000400,
    RESOURCE_TYPE_INDIRECT_BUFFER = 0x00000800,
    RESOURCE_TYPE_TEXEL_BUFFER = 0x00001000,
    RESOURCE_TYPE_RW_TEXEL_BUFFER = 0x00002000,
    RESOURCE_TYPE_RAY_TRACING = 0x00004000,
    RESOURCE_TYPE_DEPTH_STENCIL = 0x00008000,

    RESOURCE_TYPE_MAX_ENUM = 0x7FFFFFFF,
};
using ResourceType = uint32_t;

enum BufferCreationFlagBits : uint32_t {
    BUFFER_CREATION_NONE = 0x00000000,
    BUFFER_CREATION_PERSISTENT_MAP = 0x00000001,
    BUFFER_CREATION_FORCE_ALIGNMENT = 0x00000002,

    BUFFER_CREATION_MAX_ENUM = 0x7FFFFFFF,
};
using BufferCreationFlags = uint32_t;

enum TextureCreationFlagBits : uint32_t {
    TEXTURE_CREATION_NONE = 0x00000000,
    TEXTURE_CREATION_FORCE_2D = 0x00000001,
    TEXTURE_CREATION_FORCE_3D = 0x00000002,

    TEXTURE_CREATION_MAX_ENUM = 0x7FFFFFFF,
};
using TextureCreationFlags = uint32_t;

enum RHIResourceState : uint32_t {
    RESOURCE_STATE_UNDEFINED = 0,
    RESOURCE_STATE_COMMON,
    RESOURCE_STATE_TRANSFER_SRC,
    RESOURCE_STATE_TRANSFER_DST,
    RESOURCE_STATE_VERTEX_BUFFER,
    RESOURCE_STATE_INDEX_BUFFER,
    RESOURCE_STATE_COLOR_ATTACHMENT,
    RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT,
    RESOURCE_STATE_UNORDERED_ACCESS,
    RESOURCE_STATE_SHADER_RESOURCE,
    RESOURCE_STATE_INDIRECT_ARGUMENT,
    RESOURCE_STATE_PRESENT,
    RESOURCE_STATE_ACCELERATION_STRUCTURE,

    RESOURCE_STATE_MAX_ENUM,
};

enum RHIFormat : uint32_t {
    FORMAT_UKNOWN = 0,

    FORMAT_R8_SRGB,
    FORMAT_R8G8_SRGB,
    FORMAT_R8G8B8_SRGB,
    FORMAT_R8G8B8A8_SRGB,
    FORMAT_B8G8R8A8_SRGB,
    FORMAT_B8G8R8A8_UNORM,

    FORMAT_R16_SFLOAT,
    FORMAT_R16G16_SFLOAT,
    FORMAT_R16G16B16_SFLOAT,
    FORMAT_R16G16B16A16_SFLOAT,
    FORMAT_R32_SFLOAT,
    FORMAT_R32G32_SFLOAT,
    FORMAT_R32G32B32_SFLOAT,
    FORMAT_R32G32B32A32_SFLOAT,

    FORMAT_R8_UNORM,
    FORMAT_R8G8_UNORM,
    FORMAT_R8G8B8_UNORM,
    FORMAT_R8G8B8A8_UNORM,
    FORMAT_R16_UNORM,
    FORMAT_R16G16_UNORM,
    FORMAT_R16G16B16_UNORM,
    FORMAT_R16G16B16A16_UNORM,

    FORMAT_R8_SNORM,
    FORMAT_R8G8_SNORM,
    FORMAT_R8G8B8_SNORM,
    FORMAT_R8G8B8A8_SNORM,
    FORMAT_R16_SNORM,
    FORMAT_R16G16_SNORM,
    FORMAT_R16G16B16_SNORM,
    FORMAT_R16G16B16A16_SNORM,

    FORMAT_R8_UINT,
    FORMAT_R8G8_UINT,
    FORMAT_R8G8B8_UINT,
    FORMAT_R8G8B8A8_UINT,
    FORMAT_R16_UINT,
    FORMAT_R16G16_UINT,
    FORMAT_R16G16B16_UINT,
    FORMAT_R16G16B16A16_UINT,
    FORMAT_R32_UINT,
    FORMAT_R32G32_UINT,
    FORMAT_R32G32B32_UINT,
    FORMAT_R32G32B32A32_UINT,

    FORMAT_R8_SINT,
    FORMAT_R8G8_SINT,
    FORMAT_R8G8B8_SINT,
    FORMAT_R8G8B8A8_SINT,
    FORMAT_R16_SINT,
    FORMAT_R16G16_SINT,
    FORMAT_R16G16B16_SINT,
    FORMAT_R16G16B16A16_SINT,
    FORMAT_R32_SINT,
    FORMAT_R32G32_SINT,
    FORMAT_R32G32B32_SINT,
    FORMAT_R32G32B32A32_SINT,

    FORMAT_D32_SFLOAT,
    FORMAT_D32_SFLOAT_S8_UINT,
    FORMAT_D24_UNORM_S8_UINT,

    FORMAT_MAX_ENUM,
};

static uint32_t format_channel_counts(RHIFormat format) {
    switch (format) {
        case FORMAT_R8_SRGB:
        case FORMAT_R16_SFLOAT:
        case FORMAT_R32_SFLOAT:
        case FORMAT_R8_UNORM:
        case FORMAT_R16_UNORM:
        case FORMAT_R8_SNORM:
        case FORMAT_R16_SNORM:
        case FORMAT_R8_UINT:
        case FORMAT_R16_UINT:
        case FORMAT_R32_UINT:
        case FORMAT_R8_SINT:
        case FORMAT_R16_SINT:
        case FORMAT_R32_SINT:
        case FORMAT_D32_SFLOAT:
            return 1;

        case FORMAT_R8G8_SRGB:
        case FORMAT_R16G16_SFLOAT:
        case FORMAT_R32G32_SFLOAT:
        case FORMAT_R8G8_UNORM:
        case FORMAT_R16G16_UNORM:
        case FORMAT_R8G8_SNORM:
        case FORMAT_R16G16_SNORM:
        case FORMAT_R8G8_UINT:
        case FORMAT_R16G16_UINT:
        case FORMAT_R32G32_UINT:
        case FORMAT_R8G8_SINT:
        case FORMAT_R16G16_SINT:
        case FORMAT_R32G32_SINT:
        case FORMAT_D32_SFLOAT_S8_UINT:
        case FORMAT_D24_UNORM_S8_UINT:
            return 2;

        case FORMAT_R8G8B8_SRGB:
        case FORMAT_R16G16B16_SFLOAT:
        case FORMAT_R32G32B32_SFLOAT:
        case FORMAT_R8G8B8_UNORM:
        case FORMAT_R16G16B16_UNORM:
        case FORMAT_R8G8B8_SNORM:
        case FORMAT_R16G16B16_SNORM:
        case FORMAT_R8G8B8_UINT:
        case FORMAT_R16G16B16_UINT:
        case FORMAT_R32G32B32_UINT:
        case FORMAT_R8G8B8_SINT:
        case FORMAT_R16G16B16_SINT:
        case FORMAT_R32G32B32_SINT:
            return 3;

        case FORMAT_R8G8B8A8_SRGB:
        case FORMAT_B8G8R8A8_SRGB:
        case FORMAT_R16G16B16A16_SFLOAT:
        case FORMAT_R32G32B32A32_SFLOAT:
        case FORMAT_R8G8B8A8_UNORM:
        case FORMAT_R16G16B16A16_UNORM:
        case FORMAT_R8G8B8A8_SNORM:
        case FORMAT_R16G16B16A16_SNORM:
        case FORMAT_R8G8B8A8_UINT:
        case FORMAT_R16G16B16A16_UINT:
        case FORMAT_R32G32B32A32_UINT:
        case FORMAT_R8G8B8A8_SINT:
        case FORMAT_R16G16B16A16_SINT:
        case FORMAT_R32G32B32A32_SINT:
            return 4;

        default:
            return 0;
    }
}

static bool is_depth_stencil_format(RHIFormat format) {
    switch (format) {
        case FORMAT_D32_SFLOAT_S8_UINT:
        case FORMAT_D24_UNORM_S8_UINT:
            return true;
        default:
            return false;
    }
}

static bool is_depth_format(RHIFormat format) {
    switch (format) {
        case FORMAT_D32_SFLOAT:
        case FORMAT_D32_SFLOAT_S8_UINT:
        case FORMAT_D24_UNORM_S8_UINT:
            return true;
        default:
            return false;
    }
}

static bool is_stencil_format(RHIFormat format) {
    switch (format) {
        case FORMAT_D32_SFLOAT_S8_UINT:
        case FORMAT_D24_UNORM_S8_UINT:
            return true;
        default:
            return false;
    }
}

static bool is_color_format(RHIFormat format) { return !is_depth_format(format) && !is_stencil_format(format); }

static bool is_rw_format(RHIFormat format) {
    switch (format) {
        case FORMAT_D32_SFLOAT:
        case FORMAT_D32_SFLOAT_S8_UINT:
        case FORMAT_D24_UNORM_S8_UINT:
        case FORMAT_R8_SRGB:
        case FORMAT_R8G8_SRGB:
        case FORMAT_R8G8B8_SRGB:
        case FORMAT_R8G8B8A8_SRGB:
        case FORMAT_B8G8R8A8_SRGB:
            return false;
        default:
            return true;
    }
}

enum FilterType : uint32_t {
    FILTER_TYPE_NEAREST = 0,
    FILTER_TYPE_LINEAR,

    FILTER_TYPE_MAX_ENUM,
};

enum MipMapMode : uint32_t {
    MIPMAP_MODE_NEAREST = 0,
    MIPMAP_MODE_LINEAR,

    MIPMAP_MODE_MAX_ENUM_BIT,
};

enum AddressMode : uint32_t {
    ADDRESS_MODE_MIRROR,
    ADDRESS_MODE_REPEAT,
    ADDRESS_MODE_CLAMP_TO_EDGE,
    ADDRESS_MODE_CLAMP_TO_BORDER,

    ADDRESS_MODE_MAX_ENUM,
};

enum TextureViewType : uint32_t {
    VIEW_TYPE_UNDEFINED = 0,
    VIEW_TYPE_1D,
    VIEW_TYPE_2D,
    VIEW_TYPE_3D,
    VIEW_TYPE_CUBE,
    VIEW_TYPE_1D_ARRAY,
    VIEW_TYPE_2D_ARRAY,
    VIEW_TYPE_CUBE_ARRAY,

    VIEW_TYPE_MAX_ENUM,
};

enum TextureAspectFlagBits : uint32_t {
    TEXTURE_ASPECT_NONE = 0x00000000,
    TEXTURE_ASPECT_COLOR = 0x00000001,
    TEXTURE_ASPECT_DEPTH = 0x00000002,
    TEXTURE_ASPECT_STENCIL = 0x00000004,

    TEXTURE_ASPECT_DEPTH_STENCIL = TEXTURE_ASPECT_DEPTH | TEXTURE_ASPECT_STENCIL,

    TEXTURE_ASPECT_MAX_ENUM = 0x7FFFFFFF,
};
using TextureAspectFlags = uint32_t;

enum ShaderFrequencyBits : uint32_t {
    SHADER_FREQUENCY_COMPUTE = 0x00000001,
    SHADER_FREQUENCY_VERTEX = 0x00000002,
    SHADER_FREQUENCY_FRAGMENT = 0x00000004,
    SHADER_FREQUENCY_GEOMETRY = 0x00000008,
    SHADER_FREQUENCY_RAY_GEN = 0x00000010,
    SHADER_FREQUENCY_CLOSEST_HIT = 0x00000020,
    SHADER_FREQUENCY_RAY_MISS = 0x00000040,
    SHADER_FREQUENCY_INTERSECTION = 0x00000080,
    SHADER_FREQUENCY_ANY_HIT = 0x00000100,
    SHADER_FREQUENCY_MESH = 0x00000200,

    SHADER_FREQUENCY_GRAPHICS = SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT | SHADER_FREQUENCY_GEOMETRY | SHADER_FREQUENCY_MESH,
    SHADER_FREQUENCY_RAY_TRACING = SHADER_FREQUENCY_RAY_GEN | SHADER_FREQUENCY_CLOSEST_HIT | SHADER_FREQUENCY_RAY_MISS | SHADER_FREQUENCY_INTERSECTION | SHADER_FREQUENCY_ANY_HIT,
    SHADER_FREQUENCY_ALL = SHADER_FREQUENCY_GRAPHICS | SHADER_FREQUENCY_COMPUTE | SHADER_FREQUENCY_RAY_TRACING,

    SHADER_FREQUENCY_MAX_ENUM = 0x7FFFFFFF,
};
using ShaderFrequency = uint32_t;

enum AttachmentLoadOp : uint32_t {
    ATTACHMENT_LOAD_OP_LOAD = 0,
    ATTACHMENT_LOAD_OP_CLEAR,
    ATTACHMENT_LOAD_OP_DONT_CARE,

    ATTACHMENT_LOAD_OP_MAX_ENUM,
};

enum AttachmentStoreOp : uint32_t {
    ATTACHMENT_STORE_OP_STORE = 0,
    ATTACHMENT_STORE_OP_DONT_CARE = 1,

    ATTACHMENT_STORE_OP_MAX_ENUM,
};

enum PrimitiveType : uint32_t {
    PRIMITIVE_TYPE_TRIANGLE_LIST = 0,
    PRIMITIVE_TYPE_TRIANGLE_STRIP,
    PRIMITIVE_TYPE_LINE_LIST,
    PRIMITIVE_TYPE_POINT_LIST,

    PRIMITIVE_TYPE_MAX_ENUM,
};

enum RasterizerFillMode : uint32_t {
    FILL_MODE_POINT = 0,
    FILL_MODE_WIREFRAME,
    FILL_MODE_SOLID,

    FILL_MODE_MAX_ENUM,
};

enum RasterizerCullMode : uint32_t {
    CULL_MODE_NONE = 0,
    CULL_MODE_FRONT,
    CULL_MODE_BACK,

    CULL_MODE_MAX_ENUM,
};

enum RasterizerDepthClipMode : uint32_t {
    DEPTH_CLIP = 0,
    DEPTH_CLAMP,

    DEPTH_CLIP_MODE_MAX_ENUM,
};

enum CompareFunction : uint32_t {
    COMPARE_FUNCTION_LESS = 0,
    COMPARE_FUNCTION_LESS_EQUAL,
    COMPARE_FUNCTION_GREATER,
    COMPARE_FUNCTION_GREATER_EQUAL,
    COMPARE_FUNCTION_EQUAL,
    COMPARE_FUNCTION_NOT_EQUAL,
    COMPARE_FUNCTION_NEVER,
    COMPARE_FUNCTION_ALWAYS,

    COMPARE_FUNCTION_MAX_ENUM,
};

enum SamplerReductionMode : uint32_t {
    SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE = 0,
    SAMPLER_REDUCTION_MODE_MIN,
    SAMPLER_REDUCTION_MODE_MAX,

    SAMPLER_REDUCTION_MODE_MAX_ENUM,
};

enum StencilOp : uint32_t {
    STENCIL_OP_KEEP = 0,
    STENCIL_OP_ZERO,
    STENCIL_OP_REPLACE,
    STENCIL_OP_SATURATED_INCREMENT,
    STENCIL_OP_SATURATED_DECREMENT,
    STENCIL_OP_INVERT,
    STENCIL_OP_INCREMENT,
    STENCIL_OP_DECREMENT,

    STENCIL_OP_MAX_ENUM,
};

enum BlendOp : uint32_t {
    BLEND_OP_ADD = 0,
    BLEND_OP_SUBTRACT,
    BLEND_OP_REVERSE_SUBTRACT,
    BLEND_OP_MIN,
    BLEND_OP_MAX,

    BLEND_OP_MAX_ENUM,
};

enum BlendFactor : uint32_t {
    BLEND_FACTOR_ZERO = 0,
    BLEND_FACTOR_ONE,
    BLEND_FACTOR_SRC_COLOR,
    BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    BLEND_FACTOR_DST_COLOR,
    BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    BLEND_FACTOR_SRC_ALPHA,
    BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    BLEND_FACTOR_DST_ALPHA,
    BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    BLEND_FACTOR_SRC_ALPHA_SATURATE,
    BLEND_FACTOR_CONSTANT_COLOR,
    BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,

    BLEND_FACTOR_MAX_ENUM,
};

enum ColorWriteMaskBits : uint32_t {
    COLOR_MASK_RED = 0x01,
    COLOR_MASK_GREEN = 0x02,
    COLOR_MASK_BLUE = 0x04,
    COLOR_MASK_ALPHA = 0x08,

    COLOR_MASK_NONE = 0,
    COLOR_MASK_RGB = COLOR_MASK_RED | COLOR_MASK_GREEN | COLOR_MASK_BLUE,
    COLOR_MASK_RGBA = COLOR_MASK_RED | COLOR_MASK_GREEN | COLOR_MASK_BLUE | COLOR_MASK_ALPHA,
    COLOR_MASK_RG = COLOR_MASK_RED | COLOR_MASK_GREEN,
    COLOR_MASK_BA = COLOR_MASK_BLUE | COLOR_MASK_ALPHA,
};
using ColorWriteMasks = uint32_t;

struct RHIIndexedIndirectCommand {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
};

struct RHIIndirectCommand {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};

struct RHIAccelerationStructureInstanceInfo {
    float transform[3][4] = {0.0f};

    uint32_t instance_index;
    uint32_t mask;
    uint32_t shader_binding_table_offset;
    RHIBottomLevelAccelerationStructureRef blas;
};

#include "engine/core/math/extent.h"

struct TextureSubresourceRange {
    TextureAspectFlags aspect = TEXTURE_ASPECT_NONE;
    uint32_t base_mip_level = 0;
    uint32_t level_count = 0;
    uint32_t base_array_layer = 0;
    uint32_t layer_count = 0;

    uint32_t __padding = 0;

    friend bool operator==(const TextureSubresourceRange& a, const TextureSubresourceRange& b) {
        return a.aspect == b.aspect && a.base_mip_level == b.base_mip_level && a.level_count == b.level_count &&
               a.base_array_layer == b.base_array_layer && a.layer_count == b.layer_count;
    }

    bool is_default() {
        return aspect == TEXTURE_ASPECT_NONE && base_mip_level == 0 && level_count == 0 && base_array_layer == 0 && layer_count == 0;
    }
};

struct TextureSubresourceLayers {
    TextureAspectFlags aspect = TEXTURE_ASPECT_NONE;
    uint32_t mip_level = 0;
    uint32_t base_array_layer = 0;
    uint32_t layer_count = 0;

    friend bool operator==(const TextureSubresourceLayers& a, const TextureSubresourceLayers& b) {
        return a.aspect == b.aspect && a.mip_level == b.mip_level && a.base_array_layer == b.base_array_layer && a.layer_count == b.layer_count;
    }

    bool is_default() { return aspect == TEXTURE_ASPECT_NONE && mip_level == 0 && base_array_layer == 0 && layer_count == 0; }
};

struct RHIQueueInfo {
    QueueType type;
    uint32_t index;
};

struct RHISwapchainInfo {
    RHISurfaceRef surface;
    RHIQueueRef present_queue;

    uint32_t image_count;
    Extent2D extent;
    RHIFormat format;
};

struct RHICommandPoolInfo {
    RHIQueueRef queue;
};

struct RHIBufferInfo {
    uint64_t size;
    uint32_t stride = 0;
    
    MemoryUsage memory_usage = MEMORY_USAGE_GPU_ONLY;
    ResourceType type = RESOURCE_TYPE_BUFFER;

    BufferCreationFlags creation_flag = BUFFER_CREATION_NONE;
};

struct RHITextureInfo {
    RHIFormat format;
    Extent3D extent;
    uint32_t array_layers = 1;
    uint32_t mip_levels = 1;

    MemoryUsage memory_usage = MEMORY_USAGE_GPU_ONLY;
    ResourceType type = RESOURCE_TYPE_TEXTURE;

    TextureCreationFlags creation_flag = TEXTURE_CREATION_NONE;

    friend bool operator==(const RHITextureInfo& a, const RHITextureInfo& b) {
        return a.format == b.format && a.extent == b.extent && a.array_layers == b.array_layers && a.mip_levels == b.mip_levels &&
               a.memory_usage == b.memory_usage && a.type == b.type && a.creation_flag == b.creation_flag;
    }
};

struct RHITextureViewInfo {
    RHITextureRef texture;
    RHIFormat format = FORMAT_UKNOWN;
    TextureViewType view_type = VIEW_TYPE_2D;

    TextureSubresourceRange subresource = {};

    friend bool operator==(const RHITextureViewInfo& a, const RHITextureViewInfo& b) {
        return a.texture.get() == b.texture.get() && a.format == b.format && a.view_type == b.view_type && a.subresource == b.subresource;
    }
};

struct RHISamplerInfo {
    FilterType min_filter = FILTER_TYPE_LINEAR;
    FilterType mag_filter = FILTER_TYPE_LINEAR;
    MipMapMode mipmap_mode = MIPMAP_MODE_LINEAR;
    AddressMode address_mode_u = ADDRESS_MODE_REPEAT;
    AddressMode address_mode_v = ADDRESS_MODE_REPEAT;
    AddressMode address_mode_w = ADDRESS_MODE_REPEAT;
    CompareFunction compare_function = COMPARE_FUNCTION_NEVER;
    SamplerReductionMode reduction_mode = SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;

    float mip_lod_bias = 0.0f;
    float max_anisotropy = 0.0f;
};

struct RHIShaderInfo {
    std::string entry = "main";

    ShaderFrequency frequency;
    std::vector<uint8_t> code;
};

struct RHIShaderBindingTableInfo {
    void add_ray_gen_group(RHIShaderRef ray_gen_shader) { ray_gen_groups.push_back(ray_gen_shader); }
    void add_hit_group(RHIShaderRef closest_hit_shader, RHIShaderRef any_hit_shader, RHIShaderRef intersection_shader) {
        hit_groups.push_back({closest_hit_shader, any_hit_shader, intersection_shader});
    }
    void add_miss_group(RHIShaderRef ray_miss_shader) { miss_groups.push_back(ray_miss_shader); }

    struct HitGroup {
        RHIShaderRef closest_hit_shader;
        RHIShaderRef any_hit_shader;
        RHIShaderRef intersection_shader;
    };

    std::vector<RHIShaderRef> ray_gen_groups;
    std::vector<HitGroup> hit_groups;
    std::vector<RHIShaderRef> miss_groups;
};

struct RHITopLevelAccelerationStructureInfo {
    uint32_t max_instance;
    std::vector<RHIAccelerationStructureInstanceInfo> instance_infos;
};

struct RHIBottomLevelAccelerationStructureInfo {
    RHIBufferRef vertex_buffer;
    RHIBufferRef index_buffer;
    uint32_t triangle_num;
    uint32_t vertex_stride = 0;
    uint32_t index_offset = 0;
    uint32_t vertex_offset = 0;
};

struct ShaderResourceEntry {
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t size = 1;
    ShaderFrequency frequency = SHADER_FREQUENCY_ALL;

    ResourceType type = RESOURCE_TYPE_NONE;

    friend bool operator==(const ShaderResourceEntry& a, const ShaderResourceEntry& b) {
        return a.set == b.set && a.binding == b.binding && a.size == b.size && a.frequency == b.frequency && a.type == b.type;
    }
};

struct ShaderReflectInfo {
    std::string name;

    ShaderFrequency frequency;
    std::vector<ShaderResourceEntry> resources;
    std::unordered_set<std::string> defined_symbols;
    std::array<RHIFormat, MAX_SHADER_IN_OUT_VARIABLES> input_variables = {};
    std::array<RHIFormat, MAX_SHADER_IN_OUT_VARIABLES> output_variables = {};
    uint32_t local_size_x = 0;
    uint32_t local_size_y = 0;
    uint32_t local_size_z = 0;

    bool defined_symbol(std::string symbol) const { return defined_symbols.find(symbol) != defined_symbols.end(); }
};

struct PushConstantInfo {
    uint32_t size = 128;
    ShaderFrequency frequency;
};

struct AttachmentInfo {
    RHITextureViewRef texture_view = nullptr;

    AttachmentLoadOp load_op = ATTACHMENT_LOAD_OP_DONT_CARE;
    AttachmentStoreOp store_op = ATTACHMENT_STORE_OP_DONT_CARE;

    Color4 clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    float clear_depth = 1.0f;
    uint32_t clear_stencil = 0;
};

struct RHIRenderPassInfo {
    std::array<AttachmentInfo, MAX_RENDER_TARGETS> color_attachments = {};
    AttachmentInfo depth_stencil_attachment = {};

    Extent2D extent = {0, 0};
    uint32_t layers = 1;
};

struct RHIRootSignatureInfo {
    RHIRootSignatureInfo& add_push_constant(const PushConstantInfo& push_constant) {
        push_constants.push_back(push_constant);
        return *this;
    }
    RHIRootSignatureInfo& add_entry(const ShaderResourceEntry& entry);
    RHIRootSignatureInfo& add_entry(const RHIRootSignatureInfo& other);
    RHIRootSignatureInfo& add_entry_from_reflect(RHIShaderRef shader);
    const std::vector<PushConstantInfo>& get_push_constants() const { return push_constants; }
    const std::vector<ShaderResourceEntry>& get_entries() const { return entries; }

protected:
    std::vector<ShaderResourceEntry> entries;
    std::vector<PushConstantInfo> push_constants;
};

struct RHIDescriptorUpdateInfo {
    uint32_t binding = 0;
    uint32_t index = 0;

    ResourceType resource_type = RESOURCE_TYPE_NONE;

    RHIBufferRef buffer;
    RHITextureViewRef texture_view;
    RHISamplerRef sampler;
    RHITopLevelAccelerationStructureRef tlas;

    uint64_t buffer_offset = 0;
    uint64_t buffer_range = 0;
};

struct RHIRasterizerStateInfo {
    RasterizerFillMode fill_mode = FILL_MODE_SOLID;
    RasterizerCullMode cull_mode = CULL_MODE_BACK;
    RasterizerDepthClipMode depth_clip_mode = DEPTH_CLIP;

    float depth_bias = 0.0f;
    float slope_scale_depth_bias = 0.0f;

    friend bool operator==(const RHIRasterizerStateInfo& a, const RHIRasterizerStateInfo& b) {
        return a.fill_mode == b.fill_mode && a.cull_mode == b.cull_mode && a.depth_clip_mode == b.depth_clip_mode &&
               a.depth_bias == b.depth_bias && a.slope_scale_depth_bias == b.slope_scale_depth_bias;
    };
};

struct RHIDepthStencilStateInfo {
    CompareFunction depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    bool enable_depth_test = true;
    bool enable_depth_write = true;

    bool __padding[2] = {0};

    friend bool operator==(const RHIDepthStencilStateInfo& a, const RHIDepthStencilStateInfo& b) {
        return a.depth_test == b.depth_test && a.enable_depth_test == b.enable_depth_test && a.enable_depth_write == b.enable_depth_write;
    }
};

struct RHIBlendStateInfo {
    struct RenderTarget {
        BlendOp color_blend_op = BLEND_OP_ADD;
        BlendFactor color_src_blend = BLEND_FACTOR_ONE;
        BlendFactor color_dst_blend = BLEND_FACTOR_ZERO;

        BlendOp alpha_blend_op = BLEND_OP_ADD;
        BlendFactor alpha_src_blend = BLEND_FACTOR_ONE;
        BlendFactor alpha_dst_blend = BLEND_FACTOR_ZERO;

        ColorWriteMasks color_write_mask = COLOR_MASK_RGBA;

        bool enable = false;

        bool __padding[3] = {0};

        friend bool operator==(const RenderTarget& a, const RenderTarget& b) {
            return a.color_blend_op == b.color_blend_op && a.color_src_blend == b.color_src_blend && a.color_dst_blend == b.color_dst_blend &&
                   a.alpha_blend_op == b.alpha_blend_op && a.alpha_src_blend == b.alpha_src_blend && a.alpha_dst_blend == b.alpha_dst_blend &&
                   a.color_write_mask == b.color_write_mask && a.enable == b.enable;
        }
    };

    std::array<RenderTarget, MAX_RENDER_TARGETS> render_targets;

    friend bool operator==(const RHIBlendStateInfo& a, const RHIBlendStateInfo& b) { return a.render_targets == b.render_targets; }
};

struct VertexElement {
    uint32_t stream_index = 0;
    uint32_t attribute_index = 0;
    RHIFormat format = FORMAT_UKNOWN;
    uint32_t offset = 0;
    uint32_t stride = 0;
    bool use_instance_index = false;

    bool __padding[3] = {0};

    friend bool operator==(const VertexElement& a, const VertexElement& b) {
        return a.stream_index == b.stream_index && a.attribute_index == b.attribute_index && a.format == b.format && a.offset == b.offset &&
               a.stride == b.stride && a.use_instance_index == b.use_instance_index;
    }
};

struct VertexInputStateInfo {
    std::vector<VertexElement> vertex_elements;

    friend bool operator==(const VertexInputStateInfo& a, const VertexInputStateInfo& b) {
        if (a.vertex_elements.size() != b.vertex_elements.size()) return false;
        for (uint32_t i = 0; i < a.vertex_elements.size(); i++) {
            if (!(a.vertex_elements[i] == b.vertex_elements[i])) return false;
        }
        return true;
    }
};

struct RHIGraphicsPipelineInfo {
    RHIShaderRef vertex_shader;
    RHIShaderRef geometry_shader;
    RHIShaderRef fragment_shader;

    RHIRootSignatureRef root_signature;

    VertexInputStateInfo vertex_input_state = {};
    PrimitiveType primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    RHIRasterizerStateInfo rasterizer_state = {};
    RHIBlendStateInfo blend_state = {};
    RHIDepthStencilStateInfo depth_stencil_state = {};

    std::array<RHIFormat, MAX_RENDER_TARGETS> color_attachment_formats = {FORMAT_UKNOWN};
    RHIFormat depth_stencil_attachment_format = FORMAT_UKNOWN;

    uint32_t __padding = FORMAT_UKNOWN;

    friend bool operator==(const RHIGraphicsPipelineInfo& a, const RHIGraphicsPipelineInfo& b) {
        return a.vertex_shader.get() == b.vertex_shader.get() && a.geometry_shader.get() == b.geometry_shader.get() &&
               a.fragment_shader.get() == b.fragment_shader.get() && a.root_signature.get() == b.root_signature.get() &&
               a.vertex_input_state == b.vertex_input_state && a.primitive_type == b.primitive_type && a.rasterizer_state == b.rasterizer_state &&
               a.blend_state == b.blend_state && a.depth_stencil_state == b.depth_stencil_state;
    }
};

struct RHIComputePipelineInfo {
    RHIShaderRef compute_shader;

    RHIRootSignatureRef root_signature;

    friend bool operator==(const RHIComputePipelineInfo& a, const RHIComputePipelineInfo& b) {
        return a.compute_shader.get() == b.compute_shader.get() && a.root_signature.get() == b.root_signature.get();
    }
};

struct RHIRayTracingPipelineInfo {
    RHIShaderBindingTableRef shader_binding_table;

    RHIRootSignatureRef root_signature;

    friend bool operator==(const RHIRayTracingPipelineInfo& a, const RHIRayTracingPipelineInfo& b) {
        return a.shader_binding_table.get() == b.shader_binding_table.get() && a.root_signature.get() == b.root_signature.get();
    }
};

struct RHIBufferBarrier {
    RHIBufferRef buffer;
    RHIResourceState src_state;
    RHIResourceState dst_state;

    uint32_t offset = 0;
    uint32_t size = 0;
};

struct RHITextureBarrier {
    RHITextureRef texture;
    RHIResourceState src_state;
    RHIResourceState dst_state;

    TextureSubresourceRange subresource = {};
};
