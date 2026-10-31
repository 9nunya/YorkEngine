//
// Created by Nunya Bidness on 10/28/26.
//

#ifndef YORK_VISUAL_ENGINE_HXX
#define YORK_VISUAL_ENGINE_HXX

#include <memory>
#include <vector>

#include "device.hxx"
#include "renderer.hxx"
#include "render_pipeline.hxx"
#include "swapchain.hxx"
#include "visual_scene.hxx"
#include "window.hxx"

namespace york::render {
    class VisualEngine {
    public:
        VisualEngine(const int initialWidth, const int initialHeight)
            : window_(initialWidth, initialHeight, "YorkEngine"),
            device_(window_), pipeline_(device_, uniformRegistry_),
            swapchain_(device_),
            renderer_(device_, swapchain_, pipeline_, uniformRegistry_) {};
        ~VisualEngine();

        void initialize();
        [[nodiscard]] VisualScene &createScene();
        void setActiveScene(VisualScene &scene);
        void renderFrame();

        [[nodiscard]] inline VisualScene *activeScene() const
            { return activeScene_; }

        [[nodiscard]] inline Window &getWindow()
            { return window_; }
        [[nodiscard]] inline Device &getDevice()
            { return device_; }
        [[nodiscard]] inline Swapchain &getSwapchain()
            { return swapchain_; }
        [[nodiscard]] inline RenderPipeline &getPipeline()
            { return pipeline_; }
        [[nodiscard]] inline Renderer &getRenderer()
            { return renderer_; }
        [[nodiscard]] inline types::UniformRegistry &getUniformRegistry()
            { return uniformRegistry_; }

    private:
        Window window_;
        Device device_;
        types::UniformRegistry uniformRegistry_{};
        RenderPipeline pipeline_;
        Swapchain swapchain_;
        bool initialized_{false};
        std::vector<std::unique_ptr<VisualScene>> scenes_;
        VisualScene *activeScene_{nullptr};
        Renderer renderer_;
    };
}

#endif //YORK_VISUAL_ENGINE_HXX
