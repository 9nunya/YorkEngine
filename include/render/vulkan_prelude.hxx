#ifndef YORK_VULKAN_PRELUDE_HXX
#define YORK_VULKAN_PRELUDE_HXX

// Volk and Vulkan-Hpp have to agree on how the Vulkan entry points are
// reached. Volk replaces the C prototypes with its own dispatch table, so
// those prototypes must not be declared anywhere in the program.
//
//   VK_NO_PROTOTYPES              removes the prototypes from vulkan.h
//   VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
//                                 tells Vulkan-Hpp to dispatch through
//                                 function pointers, which is the only option
//                                 left once the prototypes are gone
//   VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
//                                 switches off the loader Vulkan-Hpp would
//                                 otherwise open itself, so that Volk is the
//                                 only thing that loads Vulkan. It also
//                                 gives vk::raii::Context the constructor
//                                 that takes an external proc address.
//
// The include order matters: volk.h pulls in the Vulkan headers itself and
// refuses to be used after vulkan.h has been seen without VK_NO_PROTOTYPES,
// so it has to be the first Vulkan header of the translation unit.
//
// This header is the only sanctioned way to reach the Vulkan headers. Every
// translation unit that talks to Vulkan includes it instead of vulkan.hpp
// directly, so the macros above are always in effect.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif

#ifndef VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#endif

// A swapchain that no longer matches the surface is a normal, recoverable
// condition: the application rebuilds it and carries on. Vulkan-Hpp treats
// VK_ERROR_OUT_OF_DATE_KHR as an error and throws by default, which would
// tear the render loop down on every resize. With this macro the result is
// handed back to the caller instead, where it can be handled explicitly.
#ifndef VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS 1
#endif

#include <volk.h>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#endif //YORK_VULKAN_PRELUDE_HXX
