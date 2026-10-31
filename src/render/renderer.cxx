#include "render/renderer.hxx"
#include "util/logger.hxx"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "render/visual_scene.hxx"
#include "types/fragment_material_params.hxx"

namespace york::render {
    Renderer::Renderer(Device &device, Swapchain &swapchain,
        const RenderPipeline &pipeline, types::UniformRegistry &uniformRegistry)
        : device_{device}, swapchain_{swapchain}, pipeline_{pipeline},
        uniformRegistry_{uniformRegistry} {}

    Renderer::~Renderer() {
        device_.getDevice().waitIdle();
    }

    void Renderer::initialize() {
        createUniformResources();
        createCommandPool();
        createDepthResources();
        createCommandBuffers();
        createSyncObjects();
        createRenderFinishedSemaphores();
    }

    void Renderer::createDepthResources() {
        if (swapchain_.getExtent().width == 0 || swapchain_.getExtent().height == 0) {
            // A minimized window has nothing worth rendering into; drawFrame
            // still finds a null depth buffer if it ever runs against an
            // empty extent, which also means no attachments were used.
            depthBuffer_.reset();
            return;
        }
        depthBuffer_ = std::make_unique<DepthBuffer>(
            device_, swapchain_.getExtent(), pipeline_.getDepthFormat());
    }

    void Renderer::createCommandPool() {
        vk::CommandPoolCreateInfo createInfo;
        createInfo.queueFamilyIndex = device_.findGraphicsQueueFamilyIndex();
        createInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        commandPool_ = device_.getDevice().createCommandPool(createInfo);
    }

    void Renderer::createUniformResources() {
        const auto mfTimesUbCount =
            static_cast<uint32_t>
        (maxFramesInFlight * uniformRegistry_.getUniformBuffers().size());
        const vk::DescriptorPoolSize poolSize{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = mfTimesUbCount
        };
        const vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = maxFramesInFlight,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize
        };
        descriptorPool_ = device_.getDevice().createDescriptorPool(poolInfo);

        const std::vector<vk::DescriptorSetLayout> layouts(
            maxFramesInFlight, *pipeline_.getDescriptorSetLayout());
        const vk::DescriptorSetAllocateInfo setInfo{
            .descriptorPool = *descriptorPool_,
            .descriptorSetCount = maxFramesInFlight,
            .pSetLayouts = layouts.data()
        };
        descriptorSets_ = device_.getDevice().allocateDescriptorSets(setInfo);

        for (const auto &uniformPtr : uniformRegistry_.getUniformBuffers())
            uniformPtr->createFrameBuffers(device_,
                maxFramesInFlight);

        for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
            for (const auto &uniform : uniformRegistry_.getUniformBuffers()) {
                const vk::DescriptorBufferInfo bufferInfo{
                    .buffer = uniform->bufferForFrame(frame),
                    .offset = 0,
                    .range = uniform->byteSize()
                };

                const std::array writes{
                    vk::WriteDescriptorSet{
                        .dstSet = *descriptorSets_[frame],
                        .dstBinding = uniform->meta().binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                        .pBufferInfo = &bufferInfo
                    }
                };
                device_.getDevice().updateDescriptorSets(writes, {});
            }
        }
    }

    void Renderer::createCommandBuffers() {
        vk::CommandBufferAllocateInfo allocateInfo;
        allocateInfo.commandPool = commandPool_;
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = maxFramesInFlight;

        auto allocatedBuffers = device_.getDevice().allocateCommandBuffers(allocateInfo);
        commandBuffers_.reserve(allocatedBuffers.size());
        for (auto &allocatedBuffer : allocatedBuffers)
            commandBuffers_.emplace_back(std::move(allocatedBuffer));
    }

    void Renderer::createSyncObjects() {
        vk::SemaphoreCreateInfo semaphoreCreateInfo;
        vk::FenceCreateInfo fenceCreateInfo;
        fenceCreateInfo.flags = vk::FenceCreateFlagBits::eSignaled;

        imageAvailableSemaphores_.reserve(maxFramesInFlight);
        inFlightFences_.reserve(maxFramesInFlight);
        for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
            imageAvailableSemaphores_.emplace_back(device_.getDevice().createSemaphore(semaphoreCreateInfo));
            inFlightFences_.emplace_back(device_.getDevice().createFence(fenceCreateInfo));
        }
    }

    void Renderer::createRenderFinishedSemaphores() {
        vk::SemaphoreCreateInfo semaphoreCreateInfo;
        // The number of semaphores follows the number of swapchain images, so
        // they have to be rebuilt whenever the swapchain is.
        renderFinishedSemaphores_.clear();
        renderFinishedSemaphores_.reserve(swapchain_.getImageCount());
        for (uint32_t image = 0; image < swapchain_.getImageCount(); ++image)
            renderFinishedSemaphores_.emplace_back(device_.getDevice().createSemaphore(semaphoreCreateInfo));
    }

    void Renderer::transitionImageLayout(vk::raii::CommandBuffer &commandBuffer,
                                         vk::Image image,
                                         vk::ImageLayout oldLayout,
                                         vk::ImageLayout newLayout,
                                         vk::PipelineStageFlags srcStageMask,
                                         vk::AccessFlags srcAccessMask,
                                         vk::PipelineStageFlags dstStageMask,
                                         vk::AccessFlags dstAccessMask) {
        vk::ImageMemoryBarrier barrier{
            .srcAccessMask = srcAccessMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        commandBuffer.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlags{},
                                      nullptr, nullptr, barrier);
    }

    void Renderer::updateFrameUniforms(const uint32_t frameIndex, const VisualScene &scene) {
        FrameUniforms frameUniforms{};
        if (const auto *camera = scene.activeCamera()) {
            frameUniforms.view = camera->view;
            frameUniforms.projection = camera->projection;
        }

        const auto uniformBuffers = uniformRegistry_.getUniformBuffers<FrameUniforms>();
        if (uniformBuffers.empty())
            throw std::logic_error("FrameUniforms must be registered before renderer initialization");
        uniformBuffers.front()->setData(frameUniforms);
        uniformBuffers.front()->upload(frameIndex);
    }

    void Renderer::recordCommandBuffer(const uint32_t frameIndex,
                                       const uint32_t imageIndex,
                                       const VisualScene &scene) {
        auto &commandBuffer = commandBuffers_[frameIndex];
        commandBuffer.reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        commandBuffer.begin(beginInfo);

        constexpr vk::ClearValue clearValue{
            .color = {.float32 = std::array<float, 4>{0.05f, 0.05f, 0.08f, 1.0f}}
        };

        const auto extent = swapchain_.getExtent();
        const vk::Image image = swapchain_.getImages()[imageIndex];
        const vk::ImageView imageView = swapchain_.getImageViews()[imageIndex];

        // Dynamic rendering does not transition the color attachment for the
        // application: the image has to be in COLOR_ATTACHMENT_OPTIMAL before
        // beginRendering is called. The old contents are discarded, which
        // costs nothing because the attachment is cleared anyway.
        transitionImageLayout(commandBuffer, image,
                              vk::ImageLayout::eUndefined,
                              vk::ImageLayout::eColorAttachmentOptimal,
                              vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::AccessFlagBits::eNone,
                              vk::PipelineStageFlagBits::eColorAttachmentOutput,
                              vk::AccessFlagBits::eColorAttachmentWrite);

        // The depth attachment is cleared each frame, so old contents are
        // discardable; the transition happens before the first test runs.
        const vk::ImageMemoryBarrier depthBarrier{
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = depthBuffer_->image(),
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            vk::DependencyFlags{}, nullptr, nullptr, depthBarrier);

        vk::RenderingAttachmentInfo colorAttachment;
        colorAttachment.imageView = imageView;
        colorAttachment.imageLayout =
            vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.clearValue = clearValue;

        vk::RenderingAttachmentInfo depthAttachment;
        depthAttachment.imageView = depthBuffer_->view();
        depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        // Depth is never read back, so retaining it after the pass buys nothing.
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}};

        vk::RenderingInfo renderingInfo;
        renderingInfo.renderArea = {{0, 0}, extent};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.getPipeline());

        const std::array sets{
            *descriptorSets_[frameIndex]
        };

        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *pipeline_.getPipelineLayout(),
            0,
            sets,
            {});

        const vk::Viewport viewport(0.0f,
            static_cast<float>(swapchain_.getExtent().height),
            static_cast<float>(swapchain_.getExtent().width),
            -static_cast<float>(swapchain_.getExtent().height),
            0.0f, 1.0f);
        const vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setViewport(0, viewport);
        commandBuffer.setScissor(0, scissor);

        for (const auto &object : scene.objects()) {
            if (!object.enabled() || !object.visible())
                continue;

            const auto &mesh = scene.mesh(object.meshId());
            const auto &geometry = mesh.geometry();
            const vk::Buffer vertexBuffer = geometry.getVertexBuffer();
            constexpr vk::DeviceSize vertexOffset = 0;
            commandBuffer.bindVertexBuffers(0, vertexBuffer, vertexOffset);
            commandBuffer.bindIndexBuffer(
                geometry.getIndexBuffer(), 0, vk::IndexType::eUint32);
            // The layout has one range covering both stages, so every
            // pushConstants call passes that full stage flag combination
            // (VUID-vkCmdPushConstants-offset-01796).
            constexpr vk::ShaderStageFlags pushStages =
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
            commandBuffer.pushConstants(
                *pipeline_.getPipelineLayout(),
                pushStages,
                0,
                sizeof(object.worldTransform()),
                &object.worldTransform());

            constexpr uint32_t materialFragmentOffset = sizeof(glm::mat4);
            for (const auto &primitive : mesh.primitives()) {
                // Material indices in a mesh are already scene-wide (the mesh
                // constructor rebases them), nullopt means the primitive has
                // no material and gets the white fallback set.
                const vk::DescriptorSet materialSet = primitive.materialIndex
                    ? vk::DescriptorSet(*scene.materialDescriptorSets().at(*primitive.materialIndex))
                    : scene.fallbackDescriptorSet();
                const std::array materialSets{materialSet};
                commandBuffer.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    *pipeline_.getPipelineLayout(),
                    1,
                    materialSets,
                    {});

                types::FragmentMaterialParams materialParams;
                if (primitive.materialIndex) {
                    const types::SceneMaterialData &materialData =
                        scene.materials()[*primitive.materialIndex];
                    materialParams.baseColorFactor = materialData.baseColorFactor;
                    materialParams.alphaCutoff = materialData.alphaCutoff;
                    materialParams.reservedAlphaMode =
                        static_cast<uint32_t>(materialData.alphaMode);
                }
                commandBuffer.pushConstants(
                    *pipeline_.getPipelineLayout(),
                    pushStages,
                    materialFragmentOffset,
                    sizeof(materialParams),
                    &materialParams);

                commandBuffer.drawIndexed(
                    primitive.indexCount,
                    1,
                    primitive.firstIndex,
                    0,
                    0);
            }
        }

        commandBuffer.endRendering();

        // The presentation engine reads the image, so it has to be handed
        // over in the present layout before the frame is submitted.
        transitionImageLayout(commandBuffer, image,
                              vk::ImageLayout::eColorAttachmentOptimal,
                              vk::ImageLayout::ePresentSrcKHR,
                              vk::PipelineStageFlagBits::eColorAttachmentOutput,
                              vk::AccessFlagBits::eColorAttachmentWrite,
                              vk::PipelineStageFlagBits::eBottomOfPipe,
                              vk::AccessFlagBits::eNone);

        commandBuffer.end();
    }

    void Renderer::drawFrame(const VisualScene &scene) {
        // The fence of this frame slot guards two things: the command buffer
        // that is about to be re-recorded, and the acquire semaphore that is
        // about to be signalled again. Both need the previous submission of
        // this slot to have finished, so the wait has to happen before the
        // acquire.
        const vk::Fence inFlightFence = *inFlightFences_[currentFrame_];
        if (device_.getDevice().waitForFences(inFlightFence, true, UINT64_MAX) != vk::Result::eSuccess) {
            LOG_ERROR("waiting for the in-flight fence failed");
            return;
        }

        // get the available image sephamore and pass it to the swapchain to get free next image
        const vk::Semaphore imageAvailableSemaphore = *imageAvailableSemaphores_[currentFrame_];
        const auto acquireResult = swapchain_.getSwapchain().acquireNextImage(
            UINT64_MAX, imageAvailableSemaphore);

        // The image index is undefined when the acquire did not succeed, so
        // the result has to be looked at before the index is used.
        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR) {
            LOG_INFO("swapchain is out of date, rebuilding it");
            recreate();
            return;
        }
        if (acquireResult.result == vk::Result::eSuboptimalKHR) {
            LOG_INFO("swapchain is suboptimal, recommending rebuild");
            recommendRebuild_ = true;
        }

        const uint32_t imageIndex = acquireResult.value;

        // The command buffer is recorded per frame. A buffer that was
        // recorded with the one-time-submit flag may only be submitted once,
        // and the framebuffers it refers to change when the swapchain is
        // recreated.
        // This frame slot's fence has completed, so its camera buffer is safe to update.
        updateFrameUniforms(currentFrame_, scene);
        recordCommandBuffer(currentFrame_, imageIndex, scene);

        // The fence is only reset once the submission is guaranteed to
        // follow, otherwise an early return would leave it unsignaled and
        // the next frame would wait for it forever.
        device_.getDevice().resetFences(inFlightFence);

        const vk::Semaphore renderFinishedSemaphore = *renderFinishedSemaphores_[imageIndex];
        constexpr vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        const vk::CommandBuffer commandBuffer = *commandBuffers_[currentFrame_];
        vk::SubmitInfo submitInfo;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphore;
        device_.getQueue().submit(submitInfo, inFlightFence);

        const vk::SwapchainKHR swapchain = *swapchain_.getSwapchain();
        vk::PresentInfoKHR presentInfo;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        const vk::Result presentResult = device_.getQueue().presentKHR(presentInfo);

        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR) {
            LOG_INFO("swapchain went out of date during the present, rebuilding it");
            recreate();
            return;
        }

        currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight;
    }

    void Renderer::recreate() {
        // The swapchain rebuild waits for the device to go idle, which is
        // what makes it safe to throw the old render-finished semaphores
        // away and to hand out fresh image views.
        swapchain_.recreate();
        createDepthResources();
        createRenderFinishedSemaphores();

        // we rebuilt bruh
        recommendRebuild_ = false;
    }
}
