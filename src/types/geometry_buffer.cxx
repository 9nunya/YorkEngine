#include "types/geometry_buffer.hxx"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "util/vulkan.hxx"

namespace york::types {
    namespace {
        uint32_t checkedIndexCount(const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices) {
            if (vertices.empty() || indices.empty())
                throw std::invalid_argument("geometry buffers require vertices and indices");
            if (indices.size() > std::numeric_limits<uint32_t>::max())
                throw std::length_error("geometry has too many indices for an indexed draw");
            for (const uint32_t index : indices)
                if (index >= vertices.size())
                    throw std::invalid_argument("geometry index is outside the vertex array");
            return static_cast<uint32_t>(indices.size());
        }

        util::vulkan::BufferAllocation uploadBuffer(
            render::Device &device,
            const void *data,
            const vk::DeviceSize size,
            const vk::BufferUsageFlagBits usage) {
            auto staging = util::vulkan::createBuffer(
                device,
                size,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            void *mappedData = staging.memory.mapMemory(0, size);
            std::memcpy(mappedData, data, size);
            staging.memory.unmapMemory();

            auto allocation = util::vulkan::createBuffer(
                device,
                size,
                usage | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);

            const vk::CommandBufferAllocateInfo allocateInfo{
                .commandPool = device.getTransientCommandPool(),
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            };
            const vk::raii::CommandBuffer commandBuffer =
                std::move(device.getDevice().allocateCommandBuffers(allocateInfo).front());

            commandBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
            commandBuffer.copyBuffer(*staging.buffer, *allocation.buffer, vk::BufferCopy{0, 0, size});
            commandBuffer.end();

            device.getTransferQueue().submit(
                vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer},
                nullptr);
            device.getTransferQueue().waitIdle();
            return allocation;
        }
    }

    GeometryBuffer::GeometryBuffer(const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices,
                                   render::Device &device)
        : device_{device}, indexCount_{checkedIndexCount(vertices, indices)} {
        createVertexBuffer(vertices);
        createIndexBuffer(indices);
    }

    void GeometryBuffer::createVertexBuffer(const std::vector<Vertex> &vertices) {
        const auto bufferSize = sizeof(Vertex) * vertices.size();
        auto allocation = uploadBuffer(
            device_, vertices.data(), bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
        vertexBufferMemory_ = std::move(allocation.memory);
        vertexBuffer_ = std::move(allocation.buffer);
    }

    void GeometryBuffer::createIndexBuffer(const std::vector<uint32_t> &indices) {
        const auto bufferSize = sizeof(uint32_t) * indices.size();
        auto allocation = uploadBuffer(
            device_, indices.data(), bufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
        indexBufferMemory_ = std::move(allocation.memory);
        indexBuffer_ = std::move(allocation.buffer);
    }
}
