#ifndef YORK_MESH_HXX
#define YORK_MESH_HXX

#include <vector>

#include "types/geometry_buffer.hxx"
#include "types/mesh_data.hxx"

namespace york::types {
    class Mesh {
    public:
        Mesh(const MeshData &data, render::Device &device,
             uint32_t materialIndexOffset = 0);
        Mesh(const Mesh &) = delete;
        Mesh &operator=(const Mesh &) = delete;
        Mesh(Mesh &&) = delete;
        Mesh &operator=(Mesh &&) = delete;

        [[nodiscard]] inline const GeometryBuffer &geometry() const
            { return geometry_; }
        [[nodiscard]] inline uint32_t indexCount() const
            { return geometry_.indexCount(); }
        [[nodiscard]] inline const std::vector<MeshPrimitiveData> &primitives() const
            { return primitives_; }

    private:
        std::vector<MeshPrimitiveData> primitives_;
        GeometryBuffer geometry_;
    };
}

#endif // YORK_MESH_HXX
