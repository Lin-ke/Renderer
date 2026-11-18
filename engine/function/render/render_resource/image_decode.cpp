#include "engine/function/render/render_resource/image_decode.h"

#include "engine/function/render/rhi/rhi_structs.h"

#include <DirectXTex.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace {
size_t bc_block_size(RHIFormat format) {
    switch (format) {
    case FORMAT_BC1_UNORM: case FORMAT_BC1_SRGB: return 8;
    case FORMAT_BC3_UNORM: case FORMAT_BC3_SRGB: case FORMAT_BC5_UNORM: return 16;
    default: return 0;
    }
}

RHIFormat bc_gpu_format(DXGI_FORMAT source, RHIFormat requested) {
    const bool srgb = requested == FORMAT_R8G8B8A8_SRGB;
    if (!srgb && requested != FORMAT_R8G8B8A8_UNORM) return FORMAT_UKNOWN;
    switch (source) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
        return srgb ? FORMAT_BC1_SRGB : FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
        return srgb ? FORMAT_BC3_SRGB : FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC5_UNORM:
        return srgb ? FORMAT_UKNOWN : FORMAT_BC5_UNORM;
    default: return FORMAT_UKNOWN;
    }
}
} // namespace

bool valid_bc_layout(RHIFormat format, Extent3D extent, uint32_t mip_levels) {
    return bc_block_size(format) && extent.width && extent.height && extent.width <= 16384 && extent.height <= 16384 &&
        extent.width % 4 == 0 && extent.height % 4 == 0 &&
        mip_levels > 0 && mip_levels <= std::bit_width((std::max)(extent.width, extent.height));
}

size_t bc_mip_size(RHIFormat format, Extent3D extent, uint32_t mip) {
    const size_t width = (std::max)(1u, extent.width >> mip);
    const size_t height = (std::max)(1u, extent.height >> mip);
    return ((width + 3) / 4) * ((height + 3) / 4) * bc_block_size(format);
}

std::optional<BcImage> decode_dds_bc(std::span<const uint8_t> bytes, RHIFormat requested) {
    if (bytes.size() < 4 || std::memcmp(bytes.data(), "DDS ", 4) != 0) return std::nullopt;
    DirectX::TexMetadata metadata;
    if (FAILED(DirectX::GetMetadataFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, metadata)) ||
        metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
        metadata.IsCubemap() || metadata.width > UINT32_MAX || metadata.height > UINT32_MAX) return std::nullopt;

    const RHIFormat format = bc_gpu_format(metadata.format, requested);
    const Extent3D extent{uint32_t(metadata.width), uint32_t(metadata.height), 1};
    const uint32_t mip_levels = uint32_t(metadata.mipLevels);
    if (!valid_bc_layout(format, extent, mip_levels)) return std::nullopt;

    DirectX::ScratchImage image;
    if (FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, image)))
        return std::nullopt;
    BcImage result{format, extent, mip_levels, {}};
    result.blocks.reserve(image.GetPixelsSize());
    for (uint32_t mip = 0; mip < mip_levels; ++mip) {
        const auto* level = image.GetImage(mip, 0, 0);
        const size_t size = bc_mip_size(format, extent, mip);
        if (!level || level->slicePitch != size) return std::nullopt;
        result.blocks.insert(result.blocks.end(), level->pixels, level->pixels + size);
    }
    return result;
}

std::optional<DecodedImage> decode_image_rgba(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    DecodedImage result;
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0) {
        DirectX::TexMetadata metadata;
        DirectX::ScratchImage source, converted;
        if (FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE,
            &metadata, source))) return std::nullopt;
        if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
            metadata.IsCubemap()) return std::nullopt;
        const DirectX::Image* image = source.GetImage(0, 0, 0);
        if (!image) return std::nullopt;
        if (DirectX::IsCompressed(image->format)) {
            if (FAILED(DirectX::Decompress(*image, DXGI_FORMAT_R8G8B8A8_UNORM, converted))) return std::nullopt;
            image = converted.GetImage(0, 0, 0);
        } else if (image->format != DXGI_FORMAT_R8G8B8A8_UNORM && image->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
                DirectX::TEX_FILTER_DEFAULT, 0.5f, converted))) return std::nullopt;
            image = converted.GetImage(0, 0, 0);
        }
        result.width = static_cast<uint32_t>(image->width);
        result.height = static_cast<uint32_t>(image->height);
        result.pixels.resize(size_t(result.width) * result.height * 4);
        for (uint32_t y = 0; y < result.height; ++y) {
            std::memcpy(result.pixels.data() + size_t(y) * result.width * 4,
                image->pixels + size_t(y) * image->rowPitch, size_t(result.width) * 4);
        }
    } else {
        if (bytes.size() > (std::numeric_limits<int>::max)()) return std::nullopt;
        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (!pixels) return std::nullopt;
        result.width = static_cast<uint32_t>(width);
        result.height = static_cast<uint32_t>(height);
        result.pixels.assign(pixels, pixels + size_t(width) * height * 4);
        stbi_image_free(pixels);
    }
    return result;
}
