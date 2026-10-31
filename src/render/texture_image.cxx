#include "render/texture_image.hxx"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "util/vulkan.hxx"

namespace york::render {
    TextureImage::TextureImage(Device &device, const types::ImageData &imageData)
        : device_{device}, width_{imageData.width}, height_{imageData.height} {
        if (width_ == 0 || height_ == 0)
            throw std::invalid_argument("texture dimensions must be non-zero");
        if (width_ > device_.getPhysicalDevice().getProperties().limits.maxImageDimension2D ||
            height_ > device_.getPhysicalDevice().getProperties().limits.maxImageDimension2D)
            throw std::length_error("texture dimensions exceed the device's 2D image limit");
        const uint64_t pixelBytes = static_cast<uint64_t>(width_) * height_ * 4;
        if (pixelBytes > std::numeric_limits<size_t>::max() ||
            imageData.rgbaPixels.size() != pixelBytes)
            throw std::invalid_argument("texture RGBA byte count does not match its dimensions");

        const auto formatProperties = device_.getPhysicalDevice().getFormatProperties(
            vk::Format::eR8G8B8A8Srgb);
        constexpr vk::FormatFeatureFlags requiredFeatures =
            vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
            throw std::runtime_error("device does not support sampled sRGB transfer-destination images");

        createImage();
        uploadPixels(imageData);
        createImageView();
    }

    void TextureImage::createImage() {
        const vk::ImageCreateInfo createInfo{
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Srgb,
            .extent = {width_, height_, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        image_ = device_.getDevice().createImage(createInfo);

        const vk::MemoryRequirements requirements = image_.getMemoryRequirements();
        const vk::MemoryAllocateInfo allocateInfo{
            .allocationSize = requirements.size,
            .memoryTypeIndex = util::vulkan::findMemoryType(
                device_, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
        };
        memory_ = device_.getDevice().allocateMemory(allocateInfo);
        image_.bindMemory(*memory_, 0);
    }

    void TextureImage::uploadPixels(const types::ImageData &imageData) {
        const vk::DeviceSize byteCount = imageData.rgbaPixels.size();
        auto staging = util::vulkan::createBuffer(
            device_, byteCount,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void *mapped = staging.memory.mapMemory(0, byteCount);
        std::memcpy(mapped, imageData.rgbaPixels.data(), byteCount);
        staging.memory.unmapMemory();

        const vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = device_.findGraphicsQueueFamilyIndex(),
        };
        const vk::raii::CommandPool commandPool = device_.getDevice().createCommandPool(poolInfo);
        const vk::CommandBufferAllocateInfo allocateInfo{
            .commandPool = *commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };
        const vk::raii::CommandBuffer commandBuffer =
            std::move(device_.getDevice().allocateCommandBuffers(allocateInfo).front());

        commandBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        const vk::ImageMemoryBarrier toTransfer{
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = *image_,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{}, nullptr, nullptr, toTransfer);

        const vk::BufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width_, height_, 1},
        };
        commandBuffer.copyBufferToImage(
            *staging.buffer, *image_, vk::ImageLayout::eTransferDstOptimal, copyRegion);

        const vk::ImageMemoryBarrier toShaderRead{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = *image_,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{}, nullptr, nullptr, toShaderRead);
        commandBuffer.end();

        const vk::CommandBuffer submittedBuffer = *commandBuffer;
        device_.getQueue().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &submittedBuffer}, nullptr);
        device_.getQueue().waitIdle();
    }

    void TextureImage::createImageView() {
        const vk::ImageViewCreateInfo createInfo{
            .image = *image_,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR8G8B8A8Srgb,
            .components = {
                .r = vk::ComponentSwizzle::eIdentity,
                .g = vk::ComponentSwizzle::eIdentity,
                .b = vk::ComponentSwizzle::eIdentity,
                .a = vk::ComponentSwizzle::eIdentity,
            },
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        imageView_ = device_.getDevice().createImageView(createInfo);
    }
}
