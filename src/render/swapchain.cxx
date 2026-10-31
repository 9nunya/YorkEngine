#include "render/swapchain.hxx"
#include "util/logger.hxx"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace york::render {
    Swapchain::Swapchain(Device &device)
        : device_(device) {}

    Swapchain::~Swapchain() {
        device_.getDevice().waitIdle();
        destroy();
    }

    void Swapchain::recreate() {
        device_.getDevice().waitIdle();
        destroy();
        create();
    }

    void Swapchain::initialize(vk::Format colorFormat) {
        colorFormat_ = colorFormat;
        create();
    }

    void Swapchain::create() {
        const auto surfaceCapabilities = device_.getPhysicalDevice().getSurfaceCapabilitiesKHR(device_.getSurface());
        extent_ = chooseSwapchainExtent(surfaceCapabilities);
        minImageCount_ = chooseSwapMinImageCount(surfaceCapabilities);
        colorFormat_ = chooseSurfaceFormat(
            device_.getPhysicalDevice().getSurfaceFormatsKHR(device_.getSurface()), colorFormat_).format;
        createSwapchain();
        createImageViews();
        LOG_DEBUG("swapchain extent: {}x{}, format: {}", extent_.width, extent_.height, vk::to_string(colorFormat_));
        LOG_INFO("created swapchain with {} images", swapchainImages_.size());
    }

    void Swapchain::destroy() {
        imageViews_.clear();
        swapchainImages_.clear();
        // Assigning a null handle hands the old swapchain over to the
        // temporary, whose destructor then destroys it. release() would only
        // forget the handle and leak the swapchain on every resize.
        swapchain_ = vk::raii::SwapchainKHR{nullptr};
    }

    vk::Extent2D Swapchain::chooseSwapchainExtent(
        vk::SurfaceCapabilitiesKHR const &surfaceCapabilities) const {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return surfaceCapabilities.currentExtent;
        int width;
        int height;
        glfwGetFramebufferSize(device_.getWindow().window, &width, &height);
        return {
            .width = std::clamp<uint32_t>(static_cast<uint32_t>(width), surfaceCapabilities.minImageExtent.width,
                                          surfaceCapabilities.maxImageExtent.width),
            .height = std::clamp<uint32_t>(static_cast<uint32_t>(height), surfaceCapabilities.minImageExtent.height,
                                           surfaceCapabilities.maxImageExtent.height)
        };
    }

    uint32_t Swapchain::chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities) {
        uint32_t minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if (surfaceCapabilities.maxImageCount > 0 && surfaceCapabilities.maxImageCount < minImageCount)
            minImageCount = surfaceCapabilities.maxImageCount;
        return minImageCount;
    }

    vk::SurfaceFormatKHR Swapchain::chooseSurfaceFormat(
        const std::vector<vk::SurfaceFormatKHR> &availableFormats, vk::Format requiredFormat) {
        const auto formatIt = std::ranges::find_if(availableFormats, [requiredFormat](const auto &format) {
            return format.format == requiredFormat;
        });
        if (formatIt == availableFormats.end())
            throw std::runtime_error(availableFormats.empty()
                                         ? "No surface formats are available for the swapchain"
                                         : "The surface does not support the render pass color format");
        return *formatIt;
    }

    vk::PresentModeKHR Swapchain::choosePresentMode(
        std::vector<vk::PresentModeKHR> const &availablePresentModes) {
        return std::ranges::find(availablePresentModes, vk::PresentModeKHR::eMailbox) != availablePresentModes.end()
                   ? vk::PresentModeKHR::eMailbox
                   : vk::PresentModeKHR::eFifo;
    }

    void Swapchain::createSwapchain() {
        const auto surfaceCapabilities = device_.getPhysicalDevice().getSurfaceCapabilitiesKHR(device_.getSurface());
        const auto surfaceFormat = chooseSurfaceFormat(
            device_.getPhysicalDevice().getSurfaceFormatsKHR(device_.getSurface()), colorFormat_);
        const auto presentModes = device_.getPhysicalDevice().getSurfacePresentModesKHR(device_.getSurface());
        vk::SwapchainCreateInfoKHR createInfo{
            .surface = device_.getSurface(),
            .minImageCount = minImageCount_,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent_,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = choosePresentMode(presentModes),
            .clipped = true,
            .oldSwapchain = nullptr
        };
        swapchain_ = vk::raii::SwapchainKHR(device_.getDevice(), createInfo);
        swapchainImages_ = swapchain_.getImages();
    }

    void Swapchain::createImageViews() {
        imageViews_.reserve(swapchainImages_.size());
        for (const auto image : swapchainImages_) {
            vk::ImageViewCreateInfo createInfo{
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = colorFormat_,
                .components = {
                    .r = vk::ComponentSwizzle::eIdentity,
                    .g = vk::ComponentSwizzle::eIdentity,
                    .b = vk::ComponentSwizzle::eIdentity,
                    .a = vk::ComponentSwizzle::eIdentity
                },
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            imageViews_.emplace_back(device_.getDevice().createImageView(createInfo));
        }
    }
}
