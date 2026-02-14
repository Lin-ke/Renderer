#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/core/math/math.h"
#include <vector>
#include <memory>
#include <map>

namespace render {

/**
 * @brief Simple draw batch structure for basic rendering
 */
struct DrawBatch {
    uint32_t object_id = 0;
    RHIBufferRef vertex_buffer;      // Position buffer
    RHIBufferRef normal_buffer;      // Normal buffer (for lighting)
    RHIBufferRef index_buffer;
    uint32_t index_count = 0;
    uint32_t index_offset = 0;
    Mat4 model_matrix = Mat4::Identity();
    Mat4 inv_model_matrix = Mat4::Identity();
    // MaterialRef material; //####TODO####: Material support
};

/**
 * @brief Processor for mesh pass batches
 */
class MeshPassProcessor {
public:
    virtual ~MeshPassProcessor() = default;

    /**
     * @brief Clear collected batches
     */
    void clear() { batches_.clear(); }

    /**
     * @brief Collect a batch for this pass
     * @param batch The batch to collect
     */
    void collect_batch(const DrawBatch& batch) {
        if (on_collect_batch(batch)) {
            batches_.push_back(batch);
        }
    }

    /**
     * @brief Draw all collected batches
     * @param command RHI command list
     */
    virtual void draw(RHICommandListRef command) = 0;

protected:
    /**
     * @brief Filter batches for this pass
     * @return true if the batch should be collected
     */
    virtual bool on_collect_batch(const DrawBatch& batch) { return true; }

    std::vector<DrawBatch> batches_;
};

using MeshPassProcessorRef = std::shared_ptr<MeshPassProcessor>;

/**
 * @brief Base class for passes that render meshes
 */
class MeshPass : public RenderPass {
public:
    virtual ~MeshPass() = default;

    /**
     * @brief Get the mesh pass processor
     */
    virtual MeshPassProcessorRef get_processor() const = 0;

    /**
     * @brief Set draw batches for this pass
     */
    virtual void set_draw_batches(const std::vector<DrawBatch>& batches) {
        auto processor = get_processor();
        if (processor) {
            processor->clear();
            for (const auto& batch : batches) {
                processor->collect_batch(batch);
            }
        }
    }
};

} // namespace render
