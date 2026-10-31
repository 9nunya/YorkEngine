#ifndef YORK_MESH_DATA_HXX
#define YORK_MESH_DATA_HXX

#include <cstdint>
#include <optional>
#include <vector>

#include "types/vertex.hxx"

namespace york::types {
    struct MeshPrimitiveData {
        // Indices address the combined MeshData arrays; the importer rebases local vertex indices
        // as primitives are appended, so every draw uses vertexOffset = 0.
        uint32_t firstIndex{};
        uint32_t indexCount{};
        // Index into the source SceneData material collection; rendering can resolve it later.
        std::optional<uint32_t> materialIndex{};
    };

    // CPU-side mesh data, suitable for being populated by an importer without Vulkan dependencies.
    struct MeshData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<MeshPrimitiveData> primitives;
    };
}

#endif // YORK_MESH_DATA_HXX
