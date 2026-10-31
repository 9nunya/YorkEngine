#ifndef YORK_GLTF_IMPORTER_HXX
#define YORK_GLTF_IMPORTER_HXX

#include <filesystem>

#include "types/scene_data.hxx"

namespace york::assets {
    class GltfImporter {
    public:
        // Imports the selected glTF scene into CPU data; no Vulkan resources are created here.
        [[nodiscard]] types::SceneData load(
            const std::filesystem::path &path,
            float aspectRatio = 4.0f / 3.0f) const;
    };
}

#endif // YORK_GLTF_IMPORTER_HXX
