#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/graph/rdg_builder.h"
#include <string>
#include <cstdint>

namespace render {

/**
 * @brief Pass type enumeration for all render passes
 */
enum class PassType {
    None = 0,
    Forward,
    Depth,
    GBuffer,
    DeferredLighting,
    PostProcess,
    Present,
    MaxEnum
};

/**
 * @brief Base class for all render passes
 * 
 * RenderPass defines the interface for all rendering operations.
 * Each pass implements Init() for resource creation and Build() for
 * RDG pass recording.
 */
class RenderPass {
public:
    RenderPass() = default;
    virtual ~RenderPass() = default;

    /**
     * @brief Initialize pass resources (shaders, pipelines, etc.)
     */
    virtual void init() {}

    /**
     * @brief Build the pass into the render graph
     * @param builder RDG builder for pass creation
     */
    virtual void build(RDGBuilder& builder) {}

    /**
     * @brief Get the pass name for debugging
     */
    virtual std::string_view get_name() const { return "Unknown"; }

    /**
     * @brief Get the pass type
     */
    virtual PassType get_type() const = 0;

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

protected:
    bool enabled_ = true;
};

using RenderPassRef = std::shared_ptr<RenderPass>;

} // namespace render
