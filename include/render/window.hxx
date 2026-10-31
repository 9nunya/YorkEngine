#ifndef YORK_WINDOW_HXX
#define YORK_WINDOW_HXX
#include <string>

#include "vulkan_prelude.hxx"

#include <GLFW/glfw3.h>

namespace york::render {
    class Window {
    public:
        Window(int width, int height, std::string title);
        ~Window();

        Window &operator=(const Window &) = delete;

        [[nodiscard]] vk::raii::SurfaceKHR
        createWindowSurface(const vk::raii::Instance &instance) const;

        [[nodiscard]] inline bool shouldClose() const
        { return glfwWindowShouldClose(window); }

        // Processes the events that are pending for the window.
        static void pollEvents() ;

        // True when the framebuffer was resized since the last call. The
        // flag is cleared by the call, so every resize is reported once.
        [[nodiscard]] bool consumeResize();

        [[nodiscard]] inline vk::Extent2D getExtent() const
        { return {.width = width, .height = height}; }

        GLFWwindow *window{};

        const std::string name;
        uint32_t width, height;
        bool framebufferResized = false;
    private:
        static void framebufferResizeCallback(GLFWwindow *window, int width, int height);
        void initialize();
    };
}

#endif //YORK_WINDOW_HXX
