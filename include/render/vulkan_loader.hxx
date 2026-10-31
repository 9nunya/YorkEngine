#ifndef YORK_VULKAN_LOADER_HXX
#define YORK_VULKAN_LOADER_HXX

#include <vulkan/vulkan_core.h>

namespace york::render {
    // Loads the Vulkan library and fills Volk's dispatch table.
    //
    // Safe to call more than once: the outcome of the first call is reused.
    // Throws when the library cannot be found, which is the only failure
    // mode worth reporting - everything after this point is the driver's
    // problem.
    void initializeVulkanLoader();

    // The vkGetInstanceProcAddr of the loaded Vulkan library. Only valid
    // after initializeVulkanLoader() returned without throwing.
    [[nodiscard]] PFN_vkGetInstanceProcAddr getVulkanProcAddr();

    // Loads the Vulkan library if that has not happened yet and returns its
    // proc address, which is what a vk::raii::Context is built from.
    [[nodiscard]] PFN_vkGetInstanceProcAddr requireVulkanProcAddr();
}

#endif //YORK_VULKAN_LOADER_HXX
