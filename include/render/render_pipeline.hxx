#ifndef YORK_RENDER_PIPELINE_HXX
#define YORK_RENDER_PIPELINE_HXX

#include "device.hxx"
#include "shader.hxx"
#include "types/uniform_buffer.hxx"

namespace york::render {
    class RenderPipeline {
    public:
        explicit RenderPipeline(Device &device,
            types::UniformRegistry &uniformRegistry);
        ~RenderPipeline();

        RenderPipeline(const RenderPipeline &) = delete;
        RenderPipeline &operator=(const RenderPipeline &) = delete;

        [[nodiscard]] inline const vk::raii::Pipeline &getPipeline() const
            { return pipeline_; }
        [[nodiscard]] inline const vk::raii::PipelineLayout &getPipelineLayout() const
            { return pipelineLayout_; }
        [[nodiscard]] inline vk::Format getColorFormat() {
            selectColorFormat();
            return colorFormat_;
        }
        [[nodiscard]] inline vk::Format getDepthFormat() const
            { return depthFormat_; }
        [[nodiscard]] inline const std::vector<ShaderMeta> &getShaders() const
            { return shaderMetas_; }
        [[nodiscard]] inline const vk::raii::DescriptorSetLayout &getDescriptorSetLayout() const
            { return descriptorSetLayout_; }
        // Set 1: the per-material combined image sampler VisualScene allocates into.
        [[nodiscard]] inline const vk::raii::DescriptorSetLayout &getMaterialDescriptorSetLayout() const
            { return materialDescriptorSetLayout_; }
        void pushShader(const ShaderMeta &meta);
        void initialize();

    private:
        void selectColorFormat();
        void selectDepthFormat();
        void createShaderModules();
        void createPipelineLayout();
        void createGraphicsPipeline();
        void createDescriptorSetLayout();

        bool didCompileShader(std::string_view name);

        Device &device_;
        vk::Format colorFormat_{};
        vk::Format depthFormat_{};
        std::vector<CompiledShader> shaders_;
        vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr};
        vk::raii::DescriptorSetLayout materialDescriptorSetLayout_{nullptr};
        vk::raii::PipelineLayout pipelineLayout_{nullptr};
        types::UniformRegistry &uniformRegistry_;
        // we only need the ub metas chud
        vk::raii::Pipeline pipeline_{nullptr};
        std::vector<ShaderMeta> shaderMetas_;
    };
}

#endif //YORK_RENDER_PIPELINE_HXX
