#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "assets/image_decode.hxx"

namespace york::assets {
    types::ImageData decodeImage(
        std::string name,
        std::string mimeType,
        const std::vector<uint8_t> &encodedBytes) {
        if (encodedBytes.empty() || encodedBytes.size() > std::numeric_limits<int>::max())
            throw std::runtime_error("imported image is empty or exceeds the decoder limit");

        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        stbi_uc *decoded = stbi_load_from_memory(
            encodedBytes.data(), static_cast<int>(encodedBytes.size()),
            &width, &height, &sourceChannels, STBI_rgb_alpha);
        if (decoded == nullptr)
            throw std::runtime_error("failed to decode imported image: " +
                std::string(stbi_failure_reason() == nullptr ? "unknown decoder error" : stbi_failure_reason()));
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decodedPixels{decoded, stbi_image_free};
        (void)sourceChannels;

        if (width <= 0 || height <= 0)
            throw std::runtime_error("decoded imported image has invalid dimensions");
        const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        if (pixelCount > std::numeric_limits<size_t>::max() / 4)
            throw std::length_error("decoded imported image is too large");
        const size_t rgbaByteCount = static_cast<size_t>(pixelCount * 4);

        types::ImageData imageData{
            .name = std::move(name),
            .mimeType = std::move(mimeType),
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
        };
        imageData.rgbaPixels.assign(decodedPixels.get(), decodedPixels.get() + rgbaByteCount);
        return imageData;
    }
}
