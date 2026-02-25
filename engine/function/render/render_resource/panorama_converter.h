#pragma once

#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/render_resource/texture.h"
#include <memory>

/**
 * @brief Converts equirectangular panorama texture to cubemap using RDG
 * 
 * Uses RDG Compute Pass to convert a 2D equirectangular panorama to a 6-face cubemap.
 * This is more efficient than graphics pipeline as it can write directly to each face.
 */
class PanoramaConverter {
public:
    PanoramaConverter();
    ~PanoramaConverter();

    /**
     * @brief Initialize the converter (create shaders, pipelines, etc.)
     * @return true if initialization succeeded
     */
    bool init();

    /**
     * @brief Convert panorama texture to cubemap using RDG
     * @param panorama The input panorama texture (must be 2D equirectangular)
     * @param resolution The resolution of output cubemap faces (default 512)
     * @return The generated cubemap texture, or nullptr on failure
     */
    TextureRef convert(TextureRef panorama, uint32_t resolution = 512);

    /**
     * @brief Check if converter is initialized
     */
    bool is_initialized() const { return initialized_; }

private:
    bool create_shaders();
    bool create_pipeline();
    void cleanup();

    bool initialized_ = false;

    // Compute shader for conversion
    RHIShaderRef compute_shader_;

    // Pipeline
    RHIComputePipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;

    // Sampler for panorama
    RHISamplerRef panorama_sampler_;

    // Constant buffer to replace push constants for DX11 compatibility
    std::vector<RHIBufferRef> params_buffers_;
};
