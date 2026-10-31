#include "render/device.hxx"
#include <iostream>
#include <map>

#include "util/logger.hxx"
#include "render/vulkan_loader.hxx"

const std::vector<char const *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      const vk::DebugUtilsMessageTypeFlagsEXT type,
                                                      const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                      void *pUserData) {
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning ||
        severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
        std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

    return vk::False;
}

namespace york::render {
    Device::Device(const Window &window)
        : context_{requireVulkanProcAddr()}, window_{window} {}

    Device::~Device() = default;

    std::vector<const char *> Device::getRequiredInstanceExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (enableValidationLayers)
            extensions.push_back(vk::EXTDebugUtilsExtensionName);

#if defined(__APPLE__)
        // MoltenVK is a portability implementation, so the portability
        // enumeration extension has to be part of the instance.
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

        return extensions;
    }

    static std::vector<const char *> requiredDeviceExtensions = {
        vk::KHRSwapchainExtensionName,
#if defined(__APPLE__)
        VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#endif
    };

    bool Device::isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice) {
        // Check if the physicalDevice supports the Vulkan 1.3 API version
        bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

        // Check if any of the queue families support graphics operations
        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const &qfp) {
            return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
        });

        // Check if all required physicalDevice extensions are available
        auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        bool supportsAllRequiredExtensions =
                std::ranges::all_of(requiredDeviceExtensions,
                                    [&availableDeviceExtensions](auto const &requiredDeviceExtension) {
                                        return std::ranges::any_of(availableDeviceExtensions,
                                                                   [requiredDeviceExtension](
                                                               auto const &availableDeviceExtension) {
                                                                       return strcmp(
                                                                                   availableDeviceExtension.
                                                                                   extensionName,
                                                                                   requiredDeviceExtension) == 0;
                                                                   });
                                    });

        // Check if the physicalDevice supports the required features (shader draw parameters, dynamic rendering and extended dynamic state)
        auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters
                                        &&
                                        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().
                                        extendedDynamicState;

        LOG_DEBUG(
            "supports vulkan1.3: {}, supports graphics: {}, supports required exts: {}, supports required features: {}",
            supportsVulkan1_3, supportsGraphics, supportsAllRequiredExtensions, supportsRequiredFeatures);

        // Return true if the physicalDevice meets all the criteria
        return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }

    void Device::pickPhysicalDevice() {
        const auto physicalDevices = instance_.enumeratePhysicalDevices();
        if (physicalDevices.empty())
            throw std::runtime_error("No video cards with vulkan support!");

        std::multimap<int, vk::raii::PhysicalDevice> candidates;

        // Score all physical devices by properties and features. No devices without shaders
        // in my engine, GTFO. Also checks if they are "suitable" by calling isDeviceSuitable.
        for (const auto &pd: physicalDevices) {
            const auto deviceProperties = pd.getProperties();
            uint32_t score = 0;

            if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) score += 1000;
            score += deviceProperties.limits.maxImageDimension2D;
            if (!isDeviceSuitable(pd)) continue;

            candidates.insert(std::make_pair(score, pd));
        }

        if (!candidates.empty() && candidates.rbegin()->first > 0) {
            physicalDevice_ = candidates.rbegin()->second;
            LOG_INFO("selected device: {}", physicalDevice_.getProperties().deviceName.data());
        } else
            throw std::runtime_error("No video cards suitable!");
    }

    void Device::createInstance() {
        vk::ApplicationInfo ai;
        ai.pApplicationName = "YorkEngine";
        ai.applicationVersion = VK_MAKE_VERSION(1, 4, 0);
        ai.pEngineName = "YorkEngine";
        ai.apiVersion = VK_MAKE_VERSION(1, 4, 0);

        // Get the required layers
        std::vector<char const *> requiredLayers;
        if (enableValidationLayers)
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());

        // Check if the required layers are supported by the Vulkan implementation.
        auto layerProperties = context_.enumerateInstanceLayerProperties();
        const auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
                                                             [&layerProperties](auto const &requiredLayer) {
                                                                 return std::ranges::none_of(layerProperties,
                                                                     [requiredLayer](auto const &layerProperty) {
                                                                         return strcmp(layerProperty.layerName,
                                                                                      requiredLayer) == 0;
                                                                     });
                                                             });
        if (unsupportedLayerIt != requiredLayers.end())
            throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));

        auto requiredExtensions = getRequiredInstanceExtensions();

        auto extProps = context_.enumerateInstanceExtensionProperties();
        const auto unsupportedPropertyIt =
                std::ranges::find_if(requiredExtensions,
                                     [&extProps](auto const &requiredExtension) {
                                         return std::ranges::none_of(extProps,
                                                                     [requiredExtension
                                                                     ](auto const &extensionProperty) {
                                                                         return strcmp(extensionProperty.extensionName,
                                                                                       requiredExtension) == 0;
                                                                     });
                                     });
        if (unsupportedPropertyIt != requiredExtensions.end())
            throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));

        vk::InstanceCreateInfo ci;
        ci.pApplicationInfo = &ai;

#if defined(__APPLE__)
        // MoltenVK exposes portability devices only when the portability
        // enumeration extension is enabled together with this create flag.
        ci.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif
        ci.enabledExtensionCount = requiredExtensions.size();
        ci.ppEnabledExtensionNames = requiredExtensions.data();
        ci.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
        ci.ppEnabledLayerNames = validationLayers.data();

        instance_ = vk::raii::Instance(context_, ci);
    }

    uint32_t Device::findGraphicsQueueFamilyIndex() const {
        // find the index of the first queue family that supports graphics
        const std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

        for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
            if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                    physicalDevice_.getSurfaceSupportKHR(i, *surface_))
                    return i;
        return ~0;
    }

    uint32_t Device::findTransferQueueFamilyIndex() const {
        // find the index of the first queue family that supports transfers
        const std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

        for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
            if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eTransfer))
                return i;
        return ~0;
    }

    void Device::createLogicalDevice() {
        // find the index of the first queue family that supports graphics
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports both graphics and present
        uint32_t gQueueIndex = findGraphicsQueueFamilyIndex();
        if (gQueueIndex == ~0)
            throw std::runtime_error("Could not find a queue for graphics and present -> terminating");

        // query for Vulkan 1.3 features
        vk::StructureChain<vk::PhysicalDeviceFeatures2,
                    vk::PhysicalDeviceVulkan11Features,
                    vk::PhysicalDeviceVulkan13Features,
                    vk::PhysicalDevicePortabilitySubsetFeaturesKHR,
                    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
                featureChain = {
                    {}, // vk::PhysicalDeviceFeatures2
                    {.shaderDrawParameters = true}, // vk::PhysicalDeviceVulkan11Features
                    {.dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features,
                    {},
                    {.extendedDynamicState = true} // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
                };

        // create a Device
        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = gQueueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority
        };
        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data()
        };

        device_ = vk::raii::Device(physicalDevice_, deviceCreateInfo);
        queue_ = vk::raii::Queue(device_, gQueueIndex, 0);
        transferQueue_ = vk::raii::Queue(device_, findTransferQueueFamilyIndex(), 0);
    }

    void Device::createTransientCommandPool() {
        const vk::CommandPoolCreateInfo poolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = findTransferQueueFamilyIndex(),
        };

        transientTransferPool_ = device_.createCommandPool(poolCreateInfo);
    }

    void Device::initialize() {
        createInstance();

        // setup debug messaging
        if (enableValidationLayers) {
            constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
            constexpr vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
            vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT;
            debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
            debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
            debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &debugCallback;
            messenger_ = instance_.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
        }

        pickPhysicalDevice();

        // setup surface
        surface_ = window_.createWindowSurface(instance_);

        createLogicalDevice();
        createTransientCommandPool();
    }
}
