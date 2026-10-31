//
// Created by Nunya Bidness on 10/21/26.
//

#ifndef YORK_GEOMETRY_BUFFER_HXX
#define YORK_GEOMETRY_BUFFER_HXX
#include <cstdint>
#include <vector>

#include "render/device.hxx"
#include "types/vertex.hxx"

namespace york::types {
    class GeometryBuffer {
    public:
        GeometryBuffer(const std::vector<Vertex> &vertices,
                       const std::vector<uint32_t> &indices,
                       render::Device &device);

        GeometryBuffer(const GeometryBuffer &) = delete;
        GeometryBuffer &operator=(const GeometryBuffer &) = delete;
        GeometryBuffer(GeometryBuffer &&) = delete;
        GeometryBuffer &operator=(GeometryBuffer &&) = delete;

        [[nodiscard]] inline vk::Buffer getVertexBuffer() const
            { return *vertexBuffer_; }
        [[nodiscard]] inline vk::Buffer getIndexBuffer() const
            { return *indexBuffer_; }
        [[nodiscard]] inline uint32_t indexCount() const
            { return indexCount_; }

    private:
        void createVertexBuffer(const std::vector<Vertex> &vertices);
        void createIndexBuffer(const std::vector<uint32_t> &indices);

        render::Device &device_;
        const uint32_t indexCount_;

        vk::raii::DeviceMemory vertexBufferMemory_{nullptr};
        vk::raii::Buffer vertexBuffer_{nullptr};
        vk::raii::DeviceMemory indexBufferMemory_{nullptr};
        vk::raii::Buffer indexBuffer_{nullptr};
    };
}

#endif // YORK_GEOMETRY_BUFFER_HXX
