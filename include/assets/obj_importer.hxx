#ifndef YORK_OBJ_IMPORTER_HXX
#define YORK_OBJ_IMPORTER_HXX

#include <filesystem>

#include "types/scene_data.hxx"

namespace york::assets {
    class ObjImporter {
    public:
        // Imports static Wavefront geometry and its diffuse MTL materials into
        // CPU scene data; no Vulkan resources are created here.
        [[nodiscard]] types::SceneData load(
            const std::filesystem::path &path,
            float aspectRatio = 4.0f / 3.0f) const;
    };
}

#endif // YORK_OBJ_IMPORTER_HXX
