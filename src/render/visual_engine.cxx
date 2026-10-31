//
// Created by Nunya Bidness on 10/28/26.
//
#include "render/visual_engine.hxx"

#include <algorithm>
#include <stdexcept>

#include "render/vulkan_loader.hxx"

namespace york::render {
    VisualEngine::~VisualEngine() = default;

    void VisualEngine::initialize() {
        if (initialized_)
            throw std::logic_error("VisualEngine is already initialized");

        initializeVulkanLoader(); // throws for us

        device_.initialize();

        uniformRegistry_.registerUniformBuffer<FrameUniforms>({
            .binding = 0,
            .stage = ShaderStage::Vertex
        });

        pipeline_.initialize();
        swapchain_.initialize(pipeline_.getColorFormat());
        renderer_.initialize();

        initialized_ = true;
    }

    VisualScene &VisualEngine::createScene() {
        if (!initialized_)
            throw std::logic_error("initialize VisualEngine before creating a visual scene");

        scenes_.push_back(std::make_unique<VisualScene>(device_));
        VisualScene &scene = *scenes_.back();
        if (activeScene_ == nullptr)
            activeScene_ = &scene;
        return scene;
    }

    void VisualEngine::setActiveScene(VisualScene &scene) {
        const bool owned = std::ranges::any_of(scenes_, [&scene](const auto &candidate) {
            return candidate.get() == &scene;
        });
        if (!owned)
            throw std::invalid_argument("active visual scene must be owned by this engine");
        activeScene_ = &scene;
    }

    void VisualEngine::renderFrame() {
        if (!initialized_)
            throw std::logic_error("initialize VisualEngine before rendering");
        if (activeScene_ == nullptr)
            throw std::logic_error("create or select a visual scene before rendering");
        renderer_.drawFrame(*activeScene_);
    }
}
