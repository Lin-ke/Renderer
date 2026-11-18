#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include "engine/core/math/extent.h"

enum RHIFormat : uint32_t;

struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

// Decode a single 2D image to tightly packed RGBA8. Color space is selected by
// the Texture's RHI format, so data maps keep their original channel values.
std::optional<DecodedImage> decode_image_rgba(std::span<const uint8_t> bytes);

struct BcImage {
    RHIFormat format;
    Extent3D extent;
    uint32_t mip_levels;
    std::vector<uint8_t> blocks; // Tightly packed, largest mip first
};

std::optional<BcImage> decode_dds_bc(std::span<const uint8_t> bytes, RHIFormat requested);
bool valid_bc_layout(RHIFormat format, Extent3D extent, uint32_t mip_levels);
size_t bc_mip_size(RHIFormat format, Extent3D extent, uint32_t mip);
