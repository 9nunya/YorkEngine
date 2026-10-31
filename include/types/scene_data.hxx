#ifndef YORK_SCENE_DATA_HXX
#define YORK_SCENE_DATA_HXX

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "types/mesh_data.hxx"
#include "types/texture_data.hxx"

namespace york::types {
    struct SceneObjectData {
        uint32_t meshIndex{};
        glm::mat4 worldTransform{1.0f};
        std::string name;
    };

    struct SceneCameraData {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        std::string name;
    };

    // CPU-only import result. Mesh indices in objects refer to this meshes vector.
    struct SceneData {
        std::string name;
        std::vector<ImageData> images;
        std::vector<SamplerData> samplers;
        std::vector<TextureData> textures;
        std::vector<SceneMaterialData> materials;
        std::vector<MeshData> meshes;
        std::vector<SceneObjectData> objects;
        std::vector<SceneCameraData> cameras;
        std::optional<uint32_t> activeCameraIndex;
    };
}

#endif // YORK_SCENE_DATA_HXX
