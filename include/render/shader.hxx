#ifndef YORK_SHADER_HXX
#define YORK_SHADER_HXX

#include <cstdint>
#include <string_view>
#include <vector>

#include <shaderc/shaderc.h>
#include <vulkan/vulkan_raii.hpp>

namespace york::render {
    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute,
    };

    struct ShaderMeta {
        ShaderStage stage;
        std::string_view name;

        ShaderMeta(const ShaderStage stage, const std::string_view name)
        : stage(stage), name(name) {};
    };

    struct CompiledShader {
        ShaderMeta meta;
        vk::raii::ShaderModule module{nullptr};

        CompiledShader(const ShaderMeta &meta_, vk::raii::ShaderModule &module_)
        : meta(meta_), module(std::move(module_)) {};
    };

    // Compiles GLSL to SPIR-V with shaderc.
    class ShaderCompiler {
    public:
        ShaderCompiler();
        ~ShaderCompiler();

        ShaderCompiler(const ShaderCompiler &) = delete;
        ShaderCompiler &operator=(const ShaderCompiler &) = delete;

        // Returns the SPIR-V words of the compiled shader. An empty result
        // means the compilation failed; the reason is written to the error
        // log of the compiler.
        [[nodiscard]] std::vector<uint32_t> compile(std::string_view glsl,
                                                     ShaderStage stage,
                                                     std::string_view name = "shader") const;

    private:
        shaderc_compiler_t compiler_{nullptr};
    };
}

#endif //YORK_SHADER_HXX
