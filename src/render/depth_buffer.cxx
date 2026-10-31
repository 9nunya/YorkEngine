#include "render/depth_buffer.hxx"

#include <stdexcept>

#include "util/vulkan.hxx"

namespace york::render {
    DepthBuffer::DepthBuffer(Device &device, const vk::Extent2D extent, const vk::Format format)
        : device_{device} {
        const auto formatProperties = device_.getPhysicalDevice().getFormatProperties(format);
        if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
            throw std::runtime_error("selected depth format is not supported for optimal depth attachments");

        const vk::ImageCreateInfo imageInfo{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        image_ = device_.getDevice().createImage(imageInfo);

        const vk::MemoryRequirements requirements = image_.getMemoryRequirements();
        const vk::MemoryAllocateInfo allocateInfo{
            .allocationSize = requirements.size,
            .memoryTypeIndex = util::vulkan::findMemoryType(
                device_, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
        };
        memory_ = device_.getDevice().allocateMemory(allocateInfo);
        image_.bindMemory(*memory_, 0);

        const vk::ImageViewCreateInfo viewInfo{
            .image = *image_,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .components = {
                .r = vk::ComponentSwizzle::eIdentity,
                .g = vk::ComponentSwizzle::eIdentity,
                .b = vk::ComponentSwizzle::eIdentity,
                .a = vk::ComponentSwizzle::eIdentity,
            },
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        imageView_ = device_.getDevice().createImageView(viewInfo);
    }
}
