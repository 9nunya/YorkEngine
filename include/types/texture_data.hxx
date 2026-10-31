#ifndef YORK_TEXTURE_DATA_HXX
#define YORK_TEXTURE_DATA_HXX

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace york::types {
    enum class TextureFilter {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear,
    };

    enum class TextureWrap {
        ClampToEdge,
        MirroredRepeat,
        Repeat,
    };

    enum class MaterialAlphaMode : uint32_t {
        Opaque,
        Mask,
        Blend,
    };

    struct ImageData {
        std::string name;
        std::string mimeType;
        uint32_t width{};
        uint32_t height{};
        std::vector<uint8_t> rgbaPixels;
    };

    struct SamplerData {
        TextureFilter magFilter{TextureFilter::Linear};
        TextureFilter minFilter{TextureFilter::LinearMipmapLinear};
        TextureWrap wrapS{TextureWrap::Repeat};
        TextureWrap wrapT{TextureWrap::Repeat};
    };

    struct TextureData {
        uint32_t imageIndex{};
        std::optional<uint32_t> samplerIndex;
    };

    struct TextureBindingData {
        uint32_t imageIndex{};
        uint32_t samplerIndex{};
    };

    struct SceneMaterialData {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        std::optional<uint32_t> baseColorTextureIndex;
        uint32_t baseColorTexCoord{};
        MaterialAlphaMode alphaMode{MaterialAlphaMode::Opaque};
        float alphaCutoff{0.5f};
    };
}

#endif // YORK_TEXTURE_DATA_HXX
