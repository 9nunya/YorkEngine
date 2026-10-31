#include "render/shader.hxx"

#include <iostream>
#include <stdexcept>

namespace {
    shaderc_shader_kind toShaderKind(york::render::ShaderStage stage) {
        switch (stage) {
            case york::render::ShaderStage::Vertex:
                return shaderc_vertex_shader;
            case york::render::ShaderStage::Fragment:
                return shaderc_fragment_shader;
            case york::render::ShaderStage::Compute:
                return shaderc_compute_shader;
        }
        return shaderc_vertex_shader;
    }
}

namespace york::render {
    ShaderCompiler::ShaderCompiler()
        : compiler_{shaderc_compiler_initialize()} {
        if (compiler_ == nullptr)
            throw std::runtime_error("Failed to initialise the shader compiler");
    }

    ShaderCompiler::~ShaderCompiler() {
        if (compiler_ != nullptr)
            shaderc_compiler_release(compiler_);
    }

    std::vector<uint32_t> ShaderCompiler::compile(std::string_view glsl,
                                                   ShaderStage stage,
                                                   std::string_view name) const {
        shaderc_compile_options_t options = shaderc_compile_options_initialize();
        shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
        shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan,
                                                shaderc_env_version_vulkan_1_2);
        shaderc_compile_options_set_generate_debug_info(options);
        shaderc_compile_options_set_auto_bind_uniforms(options, true);

        const std::string source{glsl};
        const std::string sourceName{name};
        const std::string entryPoint{"main"};

        shaderc_compilation_result_t result = shaderc_compile_into_spv(
            compiler_,
            source.c_str(),
            source.size(),
            toShaderKind(stage),
            sourceName.c_str(),
            entryPoint.c_str(),
            options);

        shaderc_compile_options_release(options);

        if (result == nullptr) return {};

        if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
            std::cerr << "shader compilation of \"" << sourceName << "\" failed: "
                      << shaderc_result_get_error_message(result) << '\n';
            shaderc_result_release(result);
            return {};
        }

        const auto *bytes = reinterpret_cast<const uint32_t *>(shaderc_result_get_bytes(result));
        const size_t byteCount = shaderc_result_get_length(result);

        std::vector<uint32_t> spirv(bytes, bytes + byteCount / sizeof(uint32_t));

        shaderc_result_release(result);
        return spirv;
    }
}
