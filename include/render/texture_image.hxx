#ifndef YORK_TEXTURE_IMAGE_HXX
#define YORK_TEXTURE_IMAGE_HXX

#include <cstdint>

#include "render/device.hxx"
#include "types/texture_data.hxx"

namespace york::render {
    class TextureImage {
    public:
        TextureImage(Device &device, const types::ImageData &imageData);
        TextureImage(const TextureImage &) = delete;
        TextureImage &operator=(const TextureImage &) = delete;
        TextureImage(TextureImage &&) = delete;
        TextureImage &operator=(TextureImage &&) = delete;

        [[nodiscard]] inline vk::ImageView view() const
            { return *imageView_; }
        [[nodiscard]] inline vk::Extent2D extent() const
            { return {.width = width_, .height = height_}; }

    private:
        void createImage();
        void createImageView();
        void uploadPixels(const types::ImageData &imageData);

        Device &device_;
        const uint32_t width_;
        const uint32_t height_;
        vk::raii::DeviceMemory memory_{nullptr};
        vk::raii::Image image_{nullptr};
        vk::raii::ImageView imageView_{nullptr};
    };
}

#endif // YORK_TEXTURE_IMAGE_HXX
