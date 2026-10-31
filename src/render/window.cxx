#include <utility>

#include "render/window.hxx"
#include "util/logger.hxx"

namespace york::render {
    Window::Window(const int width, const int height, std::string title)
        : name(std::move(title)), width(width), height(height) {
        initialize();
    }

    Window::~Window() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void Window::initialize() {
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW.");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(width, height,
                                  name.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

        LOG_DEBUG("initialized window \"{}\"", name);
    }

    vk::raii::SurfaceKHR Window::createWindowSurface(
        const vk::raii::Instance &instance
    ) const {
        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;

        const VkResult result = glfwCreateWindowSurface(
            *instance,
            window,
            nullptr,
            &rawSurface
        );

        if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to create window surface.");

        return {instance, rawSurface};
    }

    void Window::framebufferResizeCallback(GLFWwindow *window, const int width, const int height) {
        const auto wnd = static_cast<Window *>(glfwGetWindowUserPointer(window));
        wnd->framebufferResized = true;
        wnd->width = width;
        wnd->height = height;
    }

    void Window::pollEvents() {
        glfwPollEvents();
    }

    bool Window::consumeResize() {
        const bool resized = framebufferResized;
        framebufferResized = false;
        return resized;
    }
}
