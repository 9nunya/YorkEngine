#ifndef YORK_DEPTH_BUFFER_HXX
#define YORK_DEPTH_BUFFER_HXX

#include "render/device.hxx"

namespace york::render {
    class DepthBuffer {
    public:
        DepthBuffer(Device &device, vk::Extent2D extent, vk::Format format);
        DepthBuffer(const DepthBuffer &) = delete;
        DepthBuffer &operator=(const DepthBuffer &) = delete;
        DepthBuffer(DepthBuffer &&) = delete;
        DepthBuffer &operator=(DepthBuffer &&) = delete;

        [[nodiscard]] inline vk::Image image() const
            { return *image_; }
        [[nodiscard]] inline vk::ImageView view() const
            { return *imageView_; }

    private:
        Device &device_;
        vk::raii::DeviceMemory memory_{nullptr};
        vk::raii::Image image_{nullptr};
        vk::raii::ImageView imageView_{nullptr};
    };
}

#endif // YORK_DEPTH_BUFFER_HXX
