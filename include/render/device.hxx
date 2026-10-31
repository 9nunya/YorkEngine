#ifndef YORK_DEVICE_HXX
#define YORK_DEVICE_HXX

#include "vulkan_prelude.hxx"
#include "window.hxx"

namespace york::render {
    class Device {
    public:
        explicit Device(const Window &window);
        ~Device();
        Device(const Device &) = delete;
        Device(const Device &&) = delete;
        Device &operator=(const Device &) = delete;

        [[nodiscard]] inline vk::raii::PhysicalDevice getPhysicalDevice() const
            { return physicalDevice_; }
        [[nodiscard]] inline const vk::raii::Device &getDevice() const
            { return device_; }
        [[nodiscard]] inline vk::raii::Queue getQueue() const
            { return queue_; }
        [[nodiscard]] inline vk::raii::Queue getTransferQueue() const
            { return transferQueue_; }
        [[nodiscard]] inline const vk::raii::Context &getContext() const
            { return context_; }
        [[nodiscard]] inline const vk::raii::SurfaceKHR &getSurface() const
            { return surface_; }
        [[nodiscard]] inline const vk::raii::Instance &getInstance() const
            { return instance_; }
        [[nodiscard]] inline const Window &getWindow() const
            { return window_; }

        [[nodiscard]] uint32_t findGraphicsQueueFamilyIndex() const;
        [[nodiscard]] uint32_t findTransferQueueFamilyIndex() const;

        [[nodiscard]] inline vk::raii::CommandPool &getTransientCommandPool()
            { return transientTransferPool_; }

        // Initializes vulkan instance
        void initialize();

    private:
        // Returns required instance extensions
        // Essential for createInstance()
        static std::vector<const char *> getRequiredInstanceExtensions();

        static bool isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice);

        void pickPhysicalDevice();

        // Creates a vulkan instance
        void createInstance();

        void createLogicalDevice();

        // Useful transient command pool
        void createTransientCommandPool();

        vk::raii::Instance instance_ = nullptr;
        vk::raii::Context context_;
        vk::raii::SurfaceKHR surface_ = nullptr;
        vk::raii::DebugUtilsMessengerEXT messenger_ = nullptr;
        vk::raii::PhysicalDevice physicalDevice_ = nullptr;
        vk::raii::Device device_ = nullptr;
        vk::raii::Queue queue_ = nullptr;
        vk::raii::Queue transferQueue_ = nullptr;
        vk::raii::CommandPool transientTransferPool_ = nullptr;
        const Window &window_;
    };
}

#endif //YORK_DEVICE_HXX
