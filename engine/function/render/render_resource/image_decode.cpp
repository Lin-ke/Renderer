#include "engine/function/render/render_resource/image_decode.h"

#include <DirectXTex.h>
#include <stb/stb_image.h>
#include <cstring>
#include <limits>

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
