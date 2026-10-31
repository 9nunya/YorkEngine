#ifndef YORK_SWAPCHAIN_HXX
#define YORK_SWAPCHAIN_HXX

#include "device.hxx"

namespace york::render {
    class Swapchain {
    public:
        Swapchain(Device &device);
        ~Swapchain();

        Swapchain(const Swapchain &) = delete;
        Swapchain &operator=(const Swapchain &) = delete;

        // Rebuilds the swapchain, the image views and the framebuffers for
        // the current surface extent. Used when the window was resized.
        void recreate();

        [[nodiscard]] inline const vk::raii::SwapchainKHR &getSwapchain() const
            { return swapchain_; }
        [[nodiscard]] inline const std::vector<vk::raii::ImageView> &getImageViews() const
            { return imageViews_; }
        // The raw swapchain images. A layout transition has to name the image
        // itself, not only its view.
        [[nodiscard]] inline const std::vector<vk::Image> &getImages() const
            { return swapchainImages_; }
        [[nodiscard]] inline vk::Extent2D getExtent() const
            { return extent_; }
        [[nodiscard]] inline vk::Format getFormat() const
            { return colorFormat_; }
        [[nodiscard]] inline uint32_t getImageCount() const
            { return static_cast<uint32_t>(swapchainImages_.size()); }
        void initialize(vk::Format colorFormat);

    private:
        void create();
        void destroy();
        void createSwapchain();
        void createImageViews();

        [[nodiscard]] vk::Extent2D chooseSwapchainExtent(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities) const;
        static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities);
        static vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats,
                                                        vk::Format requiredFormat);
        static vk::PresentModeKHR choosePresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);

        Device &device_;
        vk::Format colorFormat_{};
        vk::Extent2D extent_{};
        uint32_t minImageCount_{};
        vk::raii::SwapchainKHR swapchain_{nullptr};
        std::vector<vk::Image> swapchainImages_;
        std::vector<vk::raii::ImageView> imageViews_;
    };
}

#endif //YORK_SWAPCHAIN_HXX
