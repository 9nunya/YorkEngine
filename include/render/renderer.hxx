#ifndef YORK_RENDERER_HXX
#define YORK_RENDERER_HXX

#include <cstdint>
#include <cstddef>
#include <memory>

#include "device.hxx"
#include "depth_buffer.hxx"
#include "render_pipeline.hxx"
#include "swapchain.hxx"

#include <glm/glm.hpp>

#include "types/uniform_buffer.hxx"

namespace york::render {
    class VisualScene;

    struct FrameUniforms {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
    };

    static_assert(sizeof(FrameUniforms) == 128);
    static_assert(offsetof(FrameUniforms, projection) == 64);

    class Renderer {
    public:
        Renderer(Device &device, Swapchain &swapchain,
            const RenderPipeline &pipeline, types::UniformRegistry &uniformRegistry);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        void initialize();

        // Acquires an image, submits the draw and presents it.
        void drawFrame(const VisualScene &scene);

        [[nodiscard]] uint32_t getFrameIndex() const
            { return currentFrame_; }

        // Rebuilds the swapchain and everything that depends on the number of
        // swapchain images. Has to be called when the window was resized and
        // when the swapchain reports itself as out of date.
        void recreate();

    private:
        void createCommandPool();
        void createCommandBuffers();
        void createSyncObjects();
        void createRenderFinishedSemaphores();
        void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex,
                                 const VisualScene &scene);

        // Records the layout transition of a swapchain image. Dynamic rendering
        // does not transition the color attachment for the application, so both
        // the transition into COLOR_ATTACHMENT_OPTIMAL and the handover to
        // PRESENT_SRC_KHR have to be recorded explicitly.
        static void transitionImageLayout(vk::raii::CommandBuffer &commandBuffer,
                                         vk::Image image,
                                         vk::ImageLayout oldLayout,
                                         vk::ImageLayout newLayout,
                                         vk::PipelineStageFlags srcStageMask,
                                         vk::AccessFlags srcAccessMask,
                                         vk::PipelineStageFlags dstStageMask,
                                         vk::AccessFlags dstAccessMask);

        void createUniformResources();
        void updateFrameUniforms(uint32_t frameIndex, const VisualScene &scene);
        // Callers guarantee the device is idle (initialize and the swapchain
        // recreate path both wait); the depth buffer is size-locked to the
        // swapchain extent and rebuilt together with it.
        void createDepthResources();

        vk::raii::DescriptorPool descriptorPool_{nullptr};
        std::vector<vk::raii::DescriptorSet> descriptorSets_;
        std::unique_ptr<DepthBuffer> depthBuffer_;

        static constexpr uint32_t maxFramesInFlight = 2;

        Device &device_;
        Swapchain &swapchain_;
        const RenderPipeline &pipeline_;
        types::UniformRegistry &uniformRegistry_;

        vk::raii::CommandPool commandPool_{nullptr};
        // One command buffer per frame in flight, not one per swapchain image.
        // The fence of the frame slot guards the command buffer that is about
        // to be re-recorded, so the question whether it is still in flight
        // never has to be answered by tracking image indices.
        std::vector<vk::raii::CommandBuffer> commandBuffers_;
        // One image-available semaphore per frame in flight: it is
        // signalled by the acquire and consumed by the submit of the very
        // same frame.
        std::vector<vk::raii::Semaphore> imageAvailableSemaphores_;
        // One render-finished semaphore per swapchain image, indexed by the
        // acquired image. A semaphore may not be signalled again while the
        // presentation engine may still be waiting for it, and that is
        // exactly the lifetime of one swapchain image.
        std::vector<vk::raii::Semaphore> renderFinishedSemaphores_;
        std::vector<vk::raii::Fence> inFlightFences_;
        uint32_t currentFrame_{0};
        bool recommendRebuild_{false};
    };
}

#endif //YORK_RENDERER_HXX
