#include "render/vulkan_loader.hxx"

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>

#include "volk.h"
#include "render/vulkan_prelude.hxx"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <stdexcept>
#include <string>
#include <vector>

// Vulkan-Hpp keeps its default dispatch table in a single global. The
// declaration lives in the headers, the definition has to be provided by
// exactly one translation unit of the program.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {
    VkResult loaderResult{VK_SUCCESS};
    bool loaderAttempted{false};

    // Volk's own lookup only knows the platform defaults. A Vulkan library
    // that lives outside of them stays invisible to it - Homebrew keeps the
    // loader in /opt/homebrew/lib on Apple Silicon, and that directory is
    // not one of the default search paths - so the well known locations are
    // tried as well before giving up.
    [[nodiscard]] std::vector<const char *> candidateLibraries() {
#if defined(_WIN32)
        return {"vulkan-1.dll"};
#elif defined(__APPLE__)
        return {
            "libvulkan.1.dylib",
            "/opt/homebrew/lib/libvulkan.1.dylib",
            "/opt/homebrew/lib/libvulkan.dylib",
            "/usr/local/lib/libvulkan.1.dylib",
            "/usr/local/lib/libvulkan.dylib",
            "libMoltenVK.dylib",
            "/opt/homebrew/lib/libMoltenVK.dylib",
            "/usr/local/lib/libMoltenVK.dylib",
        };
#else
        return {
            "libvulkan.so.1",
            "/usr/lib/libvulkan.so.1",
            "/usr/lib/x86_64-linux-gnu/libvulkan.so.1",
            "/usr/lib/aarch64-linux-gnu/libvulkan.so.1",
        };
#endif
    }

    // Opens a Vulkan library and hands its proc address to Volk. The handle
    // is deliberately not tracked: Volk never opened it, so Volk will not
    // close it either.
    [[nodiscard]] bool loadFromLibrary(const char *path) {
#if defined(_WIN32)
        HMODULE module = ::LoadLibraryA(path);
        if (module == nullptr) return false;

        auto *proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            reinterpret_cast<void *>(::GetProcAddress(module, "vkGetInstanceProcAddr")));
        if (proc == nullptr) {
            ::FreeLibrary(module);
            return false;
        }
#else
        void *module = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (module == nullptr) return false;

        auto *proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            ::dlsym(module, "vkGetInstanceProcAddr"));
        if (proc == nullptr) {
            ::dlclose(module);
            return false;
        }
#endif

        volkInitializeCustom(proc);
        return true;
    }
}

namespace york::render {
    void initializeVulkanLoader() {
        if (loaderAttempted) {
            if (loaderResult != VK_SUCCESS)
                throw std::runtime_error("The Vulkan library could not be loaded");
            return;
        }
        loaderAttempted = true;

        // Volk already knows where the loader lives on every supported
        // platform, so its lookup is the first thing tried.
        loaderResult = volkInitialize();
        if (loaderResult == VK_SUCCESS) return;

        for (const char *path : candidateLibraries()) {
            if (loadFromLibrary(path)) {
                loaderResult = VK_SUCCESS;
                return;
            }
        }

        throw std::runtime_error("The Vulkan library could not be loaded");
    }

    PFN_vkGetInstanceProcAddr getVulkanProcAddr() {
        return vkGetInstanceProcAddr;
    }

    PFN_vkGetInstanceProcAddr requireVulkanProcAddr() {
        initializeVulkanLoader();
        return vkGetInstanceProcAddr;
    }
}
