#ifndef YORK_VULKAN_HXX
#define YORK_VULKAN_HXX

#include <cstdint>
#include <stdexcept>

#include "render/device.hxx"
#include "render/vulkan_prelude.hxx"

namespace york::util::vulkan {
    struct BufferAllocation {
        vk::raii::DeviceMemory memory{nullptr};
        vk::raii::Buffer buffer{nullptr};
    };

    inline static uint32_t findMemoryType(
    const render::Device &device,
    uint32_t typeFilter,
    vk::MemoryPropertyFlags requiredProperties) {
        const auto properties = device.getPhysicalDevice().getMemoryProperties();

        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            const bool allowedByBuffer = (typeFilter & (1u << i)) != 0;
            const bool hasRequiredProperties =
                (properties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties;

            if (allowedByBuffer && hasRequiredProperties)
                return i;
        }

        throw std::runtime_error("failed to find a suitable Vulkan memory type");
    }

    // ya bro idc if i cant see that shit im gonna friggin newline that baby
    inline static BufferAllocation createBuffer(
        const render::Device &device,
        const vk::DeviceSize size, const vk::BufferUsageFlags usage,
        const vk::MemoryPropertyFlags properties
        ) {
        const vk::BufferCreateInfo bufferCreateInfo{
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive,
        };

        BufferAllocation allocation;
        allocation.buffer = device.getDevice().createBuffer(bufferCreateInfo);

        const vk::MemoryRequirements memoryRequirements = allocation.buffer.getMemoryRequirements();
        const vk::MemoryAllocateInfo memoryAllocateInfo{
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = findMemoryType(device, memoryRequirements.memoryTypeBits, properties),
        };

        allocation.memory = device.getDevice().allocateMemory(memoryAllocateInfo);

        allocation.buffer.bindMemory(*allocation.memory, 0);
        return allocation;
    }
}

#endif //YORK_VULKAN_HXX
