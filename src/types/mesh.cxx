#include "types/mesh.hxx"

#include <limits>
#include <stdexcept>

namespace york::types {
    namespace {
        std::vector<MeshPrimitiveData> validatedPrimitives(
            const MeshData &data, const uint32_t materialIndexOffset) {
            if (data.primitives.empty())
                throw std::invalid_argument("a mesh must contain at least one primitive");

            for (const auto &primitive : data.primitives) {
                const uint64_t endIndex = static_cast<uint64_t>(primitive.firstIndex) + primitive.indexCount;
                if (primitive.indexCount == 0 || endIndex > data.indices.size())
                    throw std::invalid_argument("mesh primitive index range is invalid");
            }
            auto primitives = data.primitives;
            for (auto &primitive : primitives)
                if (primitive.materialIndex) {
                    if (*primitive.materialIndex > std::numeric_limits<uint32_t>::max() - materialIndexOffset)
                        throw std::length_error("mesh material index exceeds the scene material range");
                    *primitive.materialIndex += materialIndexOffset;
                }
            return primitives;
        }
    }

    Mesh::Mesh(const MeshData &data, render::Device &device,
               const uint32_t materialIndexOffset)
        : primitives_{validatedPrimitives(data, materialIndexOffset)},
          geometry_{data.vertices, data.indices, device} {}
}
