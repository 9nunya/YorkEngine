# Third-party dependency resolution.
#
# Every dependency is normalised onto a stable target name so the rest of the
# build never has to care where it came from:
#
#   Vulkan::Headers            include dirs for vulkan.h and vulkan.hpp
#   volk::volk                 Volk's Vulkan loader dispatch table
#   shaderc::shaderc_combined  the combined static GLSL -> SPIR-V compiler
#
# The rule for each dependency is "use what the machine already provides,
# otherwise build it from source". That keeps a fresh checkout buildable on
# macOS, Windows and Linux without requiring a Vulkan SDK install first.

include(FetchContent)

set(YORK_VULKAN_HEADERS_VERSION "v1.4.357" CACHE STRING
    "Vulkan-Headers tag used when the machine has no Vulkan headers installed")
set(YORK_VULKAN_HPP_VERSION "v1.4.357" CACHE STRING
    "Vulkan-Hpp tag used when the machine has no Vulkan-Hpp headers installed")
set(YORK_VOLK_VERSION "vulkan-sdk-1.4.357.0" CACHE STRING
    "Volk tag used to fetch the Vulkan loader wrapper")
set(YORK_SHADERC_VERSION "v2026.4" CACHE STRING
    "shaderc tag used when the machine has no shaderc_combined installed")

# ---------------------------------------------------------------------------
# Vulkan headers: vulkan.h (C) and vulkan.hpp (C++ wrapper)
# ---------------------------------------------------------------------------
# A system install is preferred so the engine keeps using the SDK the
# developer already has. The fallback fetches the two header sets separately;
# vulkan.hpp reaches the C header through <vulkan/vulkan.h>, so two include
# roots are enough and the sets never have to be merged.

find_path(YORK_VULKAN_C_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS
        "$ENV{VULKAN_SDK}/Include"
        "$ENV{VULKAN_SDK}/include"
        "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TARGET_TRIPLET}/include"
        /opt/homebrew/include
        /usr/local/include
        /usr/include)

find_path(YORK_VULKAN_HPP_INCLUDE_DIR
    NAMES vulkan/vulkan_raii.hpp
    HINTS
        "$ENV{VULKAN_SDK}/Include"
        "$ENV{VULKAN_SDK}/include"
        "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TARGET_TRIPLET}/include"
        /opt/homebrew/include
        /usr/local/include
        /usr/include)

if(NOT YORK_VULKAN_C_INCLUDE_DIR OR NOT YORK_VULKAN_HPP_INCLUDE_DIR)
    message(STATUS "YorkEngine: fetching Vulkan-Hpp ${YORK_VULKAN_HPP_VERSION}")

    # Vulkan-Hpp carries the matching Vulkan-Headers as a submodule, so a
    # single fetch brings both header sets along.
    #
    # Its own CMake build is deliberately not used: it insists on finding a
    # Vulkan SDK, which is exactly what this fallback exists to avoid.
    # SOURCE_SUBDIR points at a directory without a CMakeLists.txt, so the
    # sources are downloaded but the project is not added.
    FetchContent_Declare(vulkan_hpp
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Hpp.git
        GIT_TAG ${YORK_VULKAN_HPP_VERSION}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES Vulkan-Headers
        SOURCE_SUBDIR vulkan)

    FetchContent_MakeAvailable(vulkan_hpp)

    set(YORK_VULKAN_C_INCLUDE_DIR "${vulkan_hpp_SOURCE_DIR}/Vulkan-Headers/include")
    set(YORK_VULKAN_HPP_INCLUDE_DIR "${vulkan_hpp_SOURCE_DIR}")
endif()

add_library(york_vulkan_headers INTERFACE)
target_include_directories(york_vulkan_headers INTERFACE
    "${YORK_VULKAN_C_INCLUDE_DIR}"
    "${YORK_VULKAN_HPP_INCLUDE_DIR}")
add_library(Vulkan::Headers ALIAS york_vulkan_headers)

message(STATUS "YorkEngine: Vulkan headers from ${YORK_VULKAN_C_INCLUDE_DIR}")

# ---------------------------------------------------------------------------
# Volk: a thin wrapper that loads the Vulkan library at runtime
# ---------------------------------------------------------------------------
# Volk is two files, so it is always built from source. Its own lookup only
# knows the platform defaults, hence VOLK_PULL_IN_VULKAN is switched off and
# the Vulkan headers resolved above are handed to it directly.

FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG ${YORK_VOLK_VERSION}
    GIT_SHALLOW TRUE)

set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(volk)

target_include_directories(volk PRIVATE "${YORK_VULKAN_C_INCLUDE_DIR}")

message(STATUS "YorkEngine: Volk ${YORK_VOLK_VERSION}")

# ---------------------------------------------------------------------------
# shaderc_combined: glslang + SPIRV-Tools + shaderc in one static archive
# ---------------------------------------------------------------------------
# Packaged builds are preferred because they save a long source build. The
# fallback compiles the same archive from upstream.

find_path(SHADERC_INCLUDE_DIR
    NAMES shaderc/shaderc.h
    HINTS
        "$ENV{VULKAN_SDK}/Include"
        "$ENV{VULKAN_SDK}/include"
        "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TARGET_TRIPLET}/include"
        /opt/homebrew/opt/shaderc/include
        /usr/local/opt/shaderc/include
        /usr/local/include
        /usr/include)

find_library(SHADERC_COMBINED_LIBRARY
    NAMES shaderc_combined
    HINTS
        "$ENV{VULKAN_SDK}/Lib"
        "$ENV{VULKAN_SDK}/lib"
        "$ENV{VULKAN_SDK}/Lib-x64"
        "$ENV{VULKAN_SDK}/Lib-X64"
        "$ENV{VULKAN_SDK}/Lib-ARM64"
        "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TARGET_TRIPLET}/lib"
        /opt/homebrew/opt/shaderc/lib
        /usr/local/opt/shaderc/lib
        /usr/local/lib
        /usr/lib
        /usr/lib64
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu)

if(SHADERC_INCLUDE_DIR AND SHADERC_COMBINED_LIBRARY)
    message(STATUS "YorkEngine: shaderc_combined from ${SHADERC_COMBINED_LIBRARY}")

    add_library(shaderc::shaderc_combined STATIC IMPORTED GLOBAL)
    set_target_properties(shaderc::shaderc_combined PROPERTIES
        IMPORTED_LOCATION "${SHADERC_COMBINED_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SHADERC_INCLUDE_DIR}")

    # The combined archive carries glslang and SPIRV-Tools along with shaderc,
    # and those reach for the platform threading and dynamic loader primitives.
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(shaderc::shaderc_combined
            INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
    endif()
else()
    message(STATUS "YorkEngine: building shaderc ${YORK_SHADERC_VERSION} from source")

    # Upstream keeps the sources of glslang, SPIRV-Tools, SPIRV-Headers and
    # abseil out of its own tree and syncs them from the DEPS file with
    # utils/git-sync-deps. That script is what the patch step runs, right
    # after the clone and before the sources are configured.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        set(YORK_PYTHON "${Python3_EXECUTABLE}")
        set(YORK_PYTHON_ARGS "")
    elseif(WIN32)
        set(YORK_PYTHON "py")
        set(YORK_PYTHON_ARGS "-3")
    else()
        set(YORK_PYTHON "python3")
        set(YORK_PYTHON_ARGS "")
    endif()

    set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
    set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
    set(SHADERC_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # SHADERC_SKIP_INSTALL stays off on purpose: it also switches off the
    # install rules of SPIRV-Tools, and glslang exports a target that links
    # against SPIRV-Tools-opt. Without that export set the generate step
    # fails with "SPIRV-Tools-opt that is not in any export set".

    FetchContent_Declare(shaderc
        GIT_REPOSITORY https://github.com/google/shaderc.git
        GIT_TAG ${YORK_SHADERC_VERSION}
        GIT_SHALLOW TRUE
        PATCH_COMMAND "${CMAKE_COMMAND}" -E chdir <SOURCE_DIR>
                      "${YORK_PYTHON}" ${YORK_PYTHON_ARGS} utils/git-sync-deps)

    FetchContent_MakeAvailable(shaderc)

    if(NOT TARGET shaderc_combined)
        message(FATAL_ERROR "YorkEngine: the shaderc source build produced no shaderc_combined target")
    endif()

    add_library(shaderc::shaderc_combined ALIAS shaderc_combined)
endif()
