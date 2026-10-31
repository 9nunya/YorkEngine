# proj header

cmake_minimum_required(VERSION 3.15)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(YorkEngine
  VERSION 0.0.1
  LANGUAGES CXX)

# dep setup

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

find_path(YORK_CGLTF_INCLUDE_DIR
  NAMES cgltf.h
  HINTS
    "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TARGET_TRIPLET}/include"
    /opt/homebrew/include
    /usr/local/include
    /usr/include)

if(NOT YORK_CGLTF_INCLUDE_DIR)
  FetchContent_Declare(york_cgltf_source
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(york_cgltf_source)
  set(YORK_CGLTF_INCLUDE_DIR "${york_cgltf_source_SOURCE_DIR}")
endif()

add_library(york_cgltf INTERFACE)
target_include_directories(york_cgltf INTERFACE "${YORK_CGLTF_INCLUDE_DIR}")

find_path(YORK_STB_INCLUDE_DIR
  NAMES stb_image.h
  HINTS
    "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TARGET_TRIPLET}/include"
    /opt/homebrew/include
    /usr/local/include
    /usr/include)

if(NOT YORK_STB_INCLUDE_DIR)
  FetchContent_Declare(york_stb_image_source
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(york_stb_image_source)
  set(YORK_STB_INCLUDE_DIR "${york_stb_image_source_SOURCE_DIR}")
endif()

add_library(york_stb_image INTERFACE)
target_include_directories(york_stb_image INTERFACE "${YORK_STB_INCLUDE_DIR}")

# build setup

file(GLOB_RECURSE SOURCE_FILES
  CONFIGURE_DEPENDS
  "src/**.cxx")

list(REMOVE_ITEM SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lab/matrix_lab.cxx"
)

add_executable(YorkEngine ${SOURCE_FILES})

add_executable(MatrixLab src/lab/matrix_lab.cxx)
target_link_libraries(MatrixLab PRIVATE glm::glm)

enable_testing()
add_executable(GltfImporterTest
  src/assets/gltf_importer.cxx
  src/assets/image_decode.cxx
  src/assets/default_camera.cxx
  tests/gltf_importer_test.cxx)
target_include_directories(GltfImporterTest PRIVATE include)
target_link_libraries(GltfImporterTest PRIVATE glm::glm york_cgltf york_stb_image)
add_test(NAME GltfImporterTest
  COMMAND GltfImporterTest mesh "${CMAKE_CURRENT_SOURCE_DIR}/assets/triangle.gltf")
add_test(NAME GltfCameraImporterTest
  COMMAND GltfImporterTest camera "${CMAKE_CURRENT_SOURCE_DIR}/assets/camera.gltf")
add_test(NAME GltfTexturedImporterTest
  COMMAND GltfImporterTest textured "${CMAKE_CURRENT_SOURCE_DIR}/assets/textured.gltf")
add_test(NAME GltfUnsupportedFeaturesTest
  COMMAND GltfImporterTest negative "${CMAKE_CURRENT_SOURCE_DIR}/assets/camera.gltf")
add_executable(ObjImporterTest
  src/assets/obj_importer.cxx
  src/assets/image_decode.cxx
  src/assets/default_camera.cxx
  tests/obj_importer_test.cxx)
target_include_directories(ObjImporterTest PRIVATE include)
target_link_libraries(ObjImporterTest PRIVATE glm::glm york_stb_image)
add_test(NAME ObjImporterTest
  COMMAND ObjImporterTest "${CMAKE_CURRENT_SOURCE_DIR}/assets/obj_importer_test.obj")
add_executable(ShaderCompileTest
  src/render/shader.cxx
  tests/shader_compile_test.cxx)
target_include_directories(ShaderCompileTest PRIVATE include)
target_link_libraries(ShaderCompileTest PRIVATE shaderc::shaderc_combined Vulkan::Headers glm::glm volk::volk)
add_test(NAME ShaderCompileTest
  COMMAND ShaderCompileTest "${CMAKE_CURRENT_SOURCE_DIR}")

# linking config

target_include_directories(YorkEngine PRIVATE include)

target_link_libraries(YorkEngine PRIVATE
  Vulkan::Headers
  volk::volk
  shaderc::shaderc_combined
  york_cgltf
  york_stb_image
  glfw
  glm::glm)

if(APPLE)
  target_link_libraries(YorkEngine PRIVATE
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework Foundation"
    "-framework IOKit")

  # GLFW loads the Vulkan library by itself with dlopen, and on macOS a
  # library outside the default search paths is only found through the
  # runtime path of the executable. Homebrew keeps the loader in
  # /opt/homebrew/lib, which is not one of those default paths.
  find_library(YORK_VULKAN_LOADER_LIBRARY
    NAMES vulkan vulkan.1 vulkan.1.4
    HINTS /opt/homebrew/lib /usr/local/lib)

  if(YORK_VULKAN_LOADER_LIBRARY)
    get_filename_component(YORK_VULKAN_LOADER_DIR
      "${YORK_VULKAN_LOADER_LIBRARY}" DIRECTORY)
    set_property(TARGET YorkEngine APPEND PROPERTY BUILD_RPATH "${YORK_VULKAN_LOADER_DIR}")
    message(STATUS "YorkEngine: Vulkan loader in ${YORK_VULKAN_LOADER_DIR}")
  endif()
endif()

add_custom_command(
  TARGET YorkEngine
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_BINARY_DIR}/compile_commands.json"
          "${CMAKE_SOURCE_DIR}/compile_commands.json"
  COMMENT "Copying compile_commands.json to the source tree"
  VERBATIM
)

# 1. Create a completely independent custom target that always runs
add_custom_target(AlwaysRunTarget)

# 2. Attach the copy command POST_BUILD to YorkEngine, but tie its execution to the target above
add_custom_command(
        TARGET YorkEngine
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:YorkEngine>/shaders"
        COMMENT "Copying shaders to bin dir..."
)

add_custom_command(
        TARGET YorkEngine
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:YorkEngine>/assets"
        COMMENT "Copying assets to bin dir..."
)

# 3. Force the engine to depend on the always-run target
# This tricks the build system into evaluating YorkEngine on every single run request
add_dependencies(YorkEngine AlwaysRunTarget)

# Volk replaces the Vulkan entry points with its own dispatch table, so the C
# prototypes must not be declared anywhere in the program. VK_NO_PROTOTYPES
# removes them and VULKAN_HPP_DISPATCH_LOADER_DYNAMIC tells Vulkan-Hpp to
# dispatch through function pointers instead of the removed prototypes.
target_compile_definitions(YorkEngine PRIVATE
  VK_NO_PROTOTYPES
  VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
  VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0
  VULKAN_HPP_NO_CONSTRUCTORS
  GLFW_INCLUDE_VULKAN
  VK_ENABLE_BETA_EXTENSIONS
  # The compiler reports absolute source paths. The logger cuts the project
  # root off them again so that the log lines stay readable.
  YORK_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
