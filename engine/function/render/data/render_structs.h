#pragma once

#include "engine/core/math/math.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include <cereal/cereal.hpp>
#include <array>
#include <cstdint>

#define MAX_POINT_LIGHT_COUNT 16
#define MAX_POINT_SHADOW_COUNT 16
#define MAX_VOLUME_LIGHT_COUNT 16
#define DIRECTIONAL_SHADOW_CASCADE_LEVEL 4
#define MAX_GIZMO_PRIMITIVE_COUNT 8192
#define CLUSTER_GROUP_SIZE 128
#define MAX_PER_FRAME_OBJECT_SIZE 16384
#define MAX_PER_FRAME_CLUSTER_SIZE 65536
#define MAX_PER_FRAME_CLUSTER_GROUP_SIZE 16384
#define MAX_SUPPORTED_MESH_PASS_COUNT 8
#define MAX_PER_PASS_PIPELINE_STATE_COUNT 512

// Geometry Primitives
struct BoundingBox {
    Vec3 min = Vec3::Zero();
    Vec3 max = Vec3::Zero();
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("min", min));
        ar(cereal::make_nvp("max", max));
    }
};

struct BoundingSphere {
    Vec3 center = Vec3::Zero();
    float radius = 0.0f;
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("center", center));
        ar(cereal::make_nvp("radius", radius));
    }
};

struct Frustum {
    Vec4 planes[6];
};

struct GlobalIconInfo {
    uint32_t camera_icon;
    uint32_t directional_light_icon;
    uint32_t point_light_icon;
};

struct RenderGlobalSetting {
    uint32_t skybox_material_id = 0;
    uint32_t cluster_inspect_mode = 0;
    uint32_t total_ticks = 0;
    float total_tick_time = 0;
    float min_frame_time = 0;

    GlobalIconInfo icons;
};

struct CameraInfo {
    Mat4 view;
    Mat4 proj;
    Mat4 prev_view;
    Mat4 prev_proj;
    Mat4 inv_view;
    Mat4 inv_proj;

    Vec3 pos;
    float _padding0;

    Vec3 front;
    float _padding1;
    Vec3 up;
    float _padding2;
    Vec3 right;
    float _padding3;

    float near_plane;
    float far_plane;
    float fov;
    float aspect;

    Frustum frustum;
};

struct ObjectInfo {
    Mat4 model;
    Mat4 prev_model;
    Mat4 inv_model;

    uint32_t animation_id;
    uint32_t material_id;
    uint32_t vertex_id;
    uint32_t index_id;
    uint32_t mesh_card_id;
    uint32_t _padding[3];

    BoundingSphere sphere;
    BoundingBox box;

    Vec4 debug_data;
};

struct VertexInfo {
    uint32_t position_id;
    uint32_t normal_id;
    uint32_t tangent_id;
    uint32_t tex_coord_id;
    uint32_t color_id;
    uint32_t bone_index_id;
    uint32_t bone_weight_id;
    uint32_t _padding;
};

struct MaterialInfo {
    float roughness;
    float metallic;
    float alpha_clip;
    float _padding;

    Vec4 diffuse;
    Vec4 emission;

    uint32_t texture_diffuse;
    uint32_t texture_normal;
    uint32_t texture_arm; // AO/Roughness/Metallic
    uint32_t texture_specular;

    std::array<int32_t, 8> ints;
    std::array<float, 8> floats;
    std::array<Vec4, 8> colors;

    std::array<uint32_t, 8> texture_2d;
    std::array<uint32_t, 4> texture_cube;
    std::array<uint32_t, 4> texture_3d;
};

struct LightClusterIndex {
    uint32_t light_id;
};

struct DirectionalLightInfo {
    Mat4 view;
    Mat4 proj;
    Vec3 pos;
    float _padding0;
    Vec3 dir;
    float depth;

    Vec3 color;
    float intensity;

    float fog_scattering;
    uint32_t cast_shadow;
    float _padding1[2];

    Frustum frustum;
    BoundingSphere sphere;
};

struct PointLightInfo {
    Mat4 view[6];
    Mat4 proj;
    Vec3 pos;
    float _padding0;
    Vec3 color;
    float intensity;
    float near_plane;
    float far_plane;
    float bias;
    float _padding1;

    float c1;
    float c2;
    union {
        uint32_t _p0;
        bool enable;
    };
    uint32_t shadow_id;

    float fog_scattering;
    float _padding2[3];

    BoundingSphere sphere;
};

struct DDGISetting {
    Vec3 grid_start_position;
    float _padding0;
    Vec3 grid_step;
    float _padding1;
    IVec3 probe_counts;
    float _padding2;

    float depth_sharpness;
    float blend_weight;
    float normal_bias;
    float energy_preservation;

    uint32_t irradiance_texture_width;
    uint32_t irradiance_texture_height;
    uint32_t depth_texture_width;
    uint32_t depth_texture_height;

    float max_probe_distance;
    int rays_per_probe;
    float _padding3[2];

    union {
        uint32_t _p0;
        bool enable;
    };
    union {
        uint32_t _p1;
        bool visibility_test;
    };
    union {
        uint32_t _p2;
        bool infinite_bounce;
    };
    union {
        uint32_t _p3;
        bool random_orientation;
    };

    BoundingBox bounding_box;
};

struct VolumeLightInfo {
    DDGISetting setting;
};

struct LightSetting {
    uint32_t directional_light_cnt = 0;
    uint32_t point_shadowed_light_cnt = 0;
    uint32_t point_light_cnt = 0;
    uint32_t volume_light_cnt = 0;

    uint32_t global_index_offset = 0;
    uint32_t _padding[3];

    uint32_t point_light_ids[MAX_POINT_LIGHT_COUNT] = {0};
    uint32_t point_shadow_light_ids[MAX_POINT_SHADOW_COUNT] = {0};

    uint32_t volume_light_ids[MAX_VOLUME_LIGHT_COUNT] = {0};
};

#define DIR_LIGHT_OFFSET (0)
#define POINT_LIGHT_OFFSET (DIR_LIGHT_OFFSET + DIRECTIONAL_SHADOW_CASCADE_LEVEL * sizeof(DirectionalLightInfo))
#define VOLUME_LIGHT_OFFSET (POINT_LIGHT_OFFSET + MAX_POINT_LIGHT_COUNT * sizeof(PointLightInfo))
#define LIGHT_SETTING_OFFSET (VOLUME_LIGHT_OFFSET + MAX_VOLUME_LIGHT_COUNT * sizeof(VolumeLightInfo))
#define LIGHT_OFFSET_MAX (LIGHT_SETTING_OFFSET + sizeof(LightSetting))

struct LightInfo {
    DirectionalLightInfo directional_lights[DIRECTIONAL_SHADOW_CASCADE_LEVEL];
    PointLightInfo point_lights[MAX_POINT_LIGHT_COUNT];
    VolumeLightInfo volume_lights[MAX_VOLUME_LIGHT_COUNT];

    LightSetting light_setting;
};

struct LightIndex {
    uint32_t index;
};

struct GizmoBoxInfo {
    Vec3 center;
    float _padding0;
    Vec3 extent;
    float _padding1;
    Vec4 color;
};

struct GizmoSphereInfo {
    Vec3 center;
    float radius;
    Vec4 color;
};

struct GizmoLineInfo {
    Vec3 from;
    float _padding0;
    Vec3 to;
    float _padding1;
    Vec4 color;
};

struct GizmoBillboardInfo {
    Vec3 center;
    uint32_t texture_id;
    Vec2 extent;
    Vec2 _padding;
    Vec4 color;
};

struct GizmoDrawData {
    RHIIndexedIndirectCommand command[4];
    GizmoBoxInfo boxes[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoSphereInfo spheres[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoLineInfo lines[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoBillboardInfo world_billboards[MAX_GIZMO_PRIMITIVE_COUNT];
};

struct MeshClusterInfo {
    uint32_t vertex_id;
    uint32_t index_id;
    uint32_t index_offset;
    float lod_error;

    BoundingSphere sphere;
};

struct MeshClusterGroupInfo {
    uint32_t cluster_id[CLUSTER_GROUP_SIZE];

    uint32_t cluster_size;
    float parent_lod_error;
    uint32_t mip_level;
    float _padding;

    BoundingSphere sphere;
};

struct MeshCardInfo {
    Vec3 view_position;
    float _padding0;
    Vec3 view_extent;
    float _padding1;
    Vec3 scale;
    float _padding2;

    Mat4 view;
    Mat4 proj;
    Mat4 inv_view;
    Mat4 inv_proj;

    UVec2 atlas_offset = UVec2::Zero();
    UVec2 atlas_extent = UVec2::Zero();
};

using MeshCardReadBack = std::array<uint32_t, MAX_PER_FRAME_OBJECT_SIZE * 6>;

struct IndirectSetting {
    uint32_t process_size = 0;
    uint32_t pipeline_state_size = 0;
    uint32_t _padding0[2];

    uint32_t draw_size = 0;
    uint32_t frustum_cull = 0;
    uint32_t occlusion_cull = 0;
    uint32_t _padding1;
};

struct IndirectMeshDrawInfo {
    uint32_t object_id = 0;
    uint32_t command_id = 0;
};

struct DrawClusterGroupDatas {
    IndirectSetting setting;
    IndirectMeshDrawInfo draws[MAX_PER_FRAME_OBJECT_SIZE];
};

struct IndirectMeshDrawCommands {
    RHIIndirectCommand commands[MAX_PER_FRAME_OBJECT_SIZE];
};

struct IndirectClusterDrawInfo {
    uint32_t object_id = 0;
    uint32_t cluster_id = 0;
    uint32_t command_id = 0;
    uint32_t _padding;
};

struct MeshClusterDrawInfo {
    uint32_t object_id = 0;
    uint32_t cluster_id = 0;
};

struct IndirectClusterDrawDatas {
    IndirectSetting setting;
    IndirectClusterDrawInfo draws[MAX_PER_FRAME_CLUSTER_SIZE];
};

struct IndirectClusterDrawCommands {
    RHIIndirectCommand command[MAX_PER_PASS_PIPELINE_STATE_COUNT];
};

struct IndirectClusterGroupDrawInfo {
    uint32_t object_id = 0;
    uint32_t cluster_group_id = 0;
    uint32_t command_id = 0;
    uint32_t _padding;
};

struct IndirectClusterGroupDrawDatas {
    IndirectSetting setting;
    IndirectClusterGroupDrawInfo draws[MAX_PER_FRAME_CLUSTER_GROUP_SIZE];
};
