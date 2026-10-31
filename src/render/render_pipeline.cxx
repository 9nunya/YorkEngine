#include "render/render_pipeline.hxx"

#include <array>
#include <cstddef>
#include <filesystem>

#include "render/shader.hxx"
#include "types/fragment_material_params.hxx"
#include "util/logger.hxx"

#include <stdexcept>
#include <string_view>

#include "types/vertex.hxx"
#include "util/fs.hxx"

namespace york::render {
    namespace {
        // Set 1 binding 0: the per-material base-color texture. Matches
        // layout(set = 1, binding = 0) in the fragment shader.
        constexpr vk::DescriptorSetLayoutBinding materialSamplerBindingInfo{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        };
    }

    RenderPipeline::RenderPipeline(Device &device,
        types::UniformRegistry &uniformRegistry)
        : device_{device}, uniformRegistry_{uniformRegistry} {}

    RenderPipeline::~RenderPipeline() = default;

    void RenderPipeline::initialize() {
        selectColorFormat();
        selectDepthFormat();
        createShaderModules();
        // createRenderPass(); fuck render passes bro
        createDescriptorSetLayout();
        createPipelineLayout();
        createGraphicsPipeline();
    }

    void RenderPipeline::pushShader(const ShaderMeta &meta) {
        shaderMetas_.emplace_back(meta);
    }

    bool RenderPipeline::didCompileShader(std::string_view name) {
        return std::ranges::any_of(shaders_.begin(), shaders_.end(), [&name](const auto &shader) {
            return shader.meta.name == name;
        });
    }

    void RenderPipeline::selectColorFormat() {
        const auto formats = device_.getPhysicalDevice().getSurfaceFormatsKHR(device_.getSurface());
        if (formats.empty())
            throw std::runtime_error("No surface formats are available for the render pipeline");

        const auto preferredFormat = std::ranges::find_if(formats, [](const auto &format) {
            return format.format == vk::Format::eB8G8R8A8Srgb;
        });
        colorFormat_ = preferredFormat != formats.end() ? preferredFormat->format : formats.front().format;
        LOG_INFO("selected render pipeline colour format: {}", vk::to_string(colorFormat_));
    }

    void RenderPipeline::selectDepthFormat() {
        if (depthFormat_ != vk::Format::eUndefined)
            return;

        // 32-bit depth first, falling back to packed depth-stencil layouts.
        // The first candidate the device supports as a depth-stencil
        // attachment wins, deterministically.
        constexpr vk::Format depthCandidates[]{
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint,
        };
        constexpr vk::FormatFeatureFlags requiredFeatures =
            vk::FormatFeatureFlagBits::eDepthStencilAttachment;
        depthFormat_ = vk::Format::eUndefined;
        for (const vk::Format candidate : depthCandidates) {
            const vk::FormatProperties properties =
                device_.getPhysicalDevice().getFormatProperties(candidate);
            if ((properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures) {
                depthFormat_ = candidate;
                break;
            }
        }
        if (depthFormat_ == vk::Format::eUndefined)
            throw std::runtime_error("no device depth format is supported for the render pipeline");
        LOG_INFO("selected render pipeline depth format: {}", vk::to_string(depthFormat_));
    }

    void RenderPipeline::createShaderModules() {
        std::vector<CompiledShader> compiled{};

        // all shaders (should) exist in bin dir (please), under shaders/, and match name as filename
        // (ext included in name)
        const auto root = std::filesystem::current_path() / "shaders";
        const ShaderCompiler compiler;

        for (const auto &shader : shaderMetas_) {
            // dont compile shaders we already compiled
            if (didCompileShader(shader.name)) continue;

            // compiling ritual bruh
            bool readFailed = false;
            std::string shaderSource;
            try {
                shaderSource = util::fs::read_file_string(root / shader.name);
            } catch (const std::runtime_error &e) {
                readFailed = true;
            }

            if (readFailed || shaderSource.empty()) {
                LOG_WARN("Shader {} could not be loaded, because the file could not be read. skipping",
                    shader.name);
                continue;
            }

            const auto words = compiler.compile(shaderSource, shader.stage, shader.name);
            if (words.empty())
                throw std::runtime_error(std::format("could not compile shader {}", shader.name));

            vk::ShaderModuleCreateInfo fragmentCreateInfo;
            fragmentCreateInfo.codeSize = words.size() * sizeof(uint32_t);
            fragmentCreateInfo.pCode = words.data();
            auto deviceShaderModule = device_.getDevice().createShaderModule(fragmentCreateInfo);

            compiled.emplace_back(shader, deviceShaderModule);
        }

        shaders_ = std::move(compiled);
    }

    void RenderPipeline::createPipelineLayout() {
        const vk::DescriptorSetLayout setLayout = *descriptorSetLayout_;
        const vk::DescriptorSetLayout materialLayout = *materialDescriptorSetLayout_;
        std::array<vk::DescriptorSetLayout, 2> setLayouts{setLayout, materialLayout};
        // Both shaders declare the same shared push block: the model matrix
        // occupies global bytes [0, 64) and the material parameters land at
        // [64, 88). One range covering the whole block keeps every stage's
        // block contained in a range it can index directly.
        constexpr vk::PushConstantRange pushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(glm::mat4) + sizeof(types::FragmentMaterialParams),
        };
        const vk::PipelineLayoutCreateInfo createInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange,
        };
        pipelineLayout_ = device_.getDevice().createPipelineLayout(createInfo);
    }

    void RenderPipeline::createDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> bindings{};
        bindings.reserve(uniformRegistry_.getUniformBuffers().size());
        for (const auto &uniformBuffer : uniformRegistry_.getUniformBuffers())
            bindings.emplace_back(vk::DescriptorSetLayoutBinding {
                .binding = uniformBuffer->meta().binding,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags =
                    uniformBuffer->meta().stage
                    == ShaderStage::Vertex ? vk::ShaderStageFlagBits::eVertex
                    : uniformBuffer->meta().stage == ShaderStage::Fragment
                    ? vk::ShaderStageFlagBits::eFragment
                    : vk::ShaderStageFlagBits::eCompute,
            });

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };

        descriptorSetLayout_ = vk::raii::DescriptorSetLayout(device_.getDevice(), layoutInfo);

        // Set 1: the per-material base-color texture. One combined image
        // sampler per material; VisualScene allocates a set per material from
        // its own pool, images and samplers included.
        materialDescriptorSetLayout_ = device_.getDevice().createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = 1,
                .pBindings = &materialSamplerBindingInfo,
            });
    }

    void RenderPipeline::createGraphicsPipeline() {
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages{};
        shaderStages.reserve(shaders_.size());

        // create shader info
        for (const auto &shader : shaders_)
            shaderStages.emplace_back(vk::PipelineShaderStageCreateInfo {
                .stage = shader.meta.stage == ShaderStage::Vertex ? vk::ShaderStageFlagBits::eVertex
                    : shader.meta.stage == ShaderStage::Fragment ? vk::ShaderStageFlagBits::eFragment
                    : vk::ShaderStageFlagBits::eCompute,
                .module = shader.module,
                .pName = "main"
            });

        // set pnext fucking annoying as fucking siht i frickign hate this
        //for (int i = 0; i < shaderStages.size(); ++i)
        //    if (i + 1 != shaderStages.size())
        //        shaderStages[i].pNext = &shaderStages[i + 1];
        // nvm i think im the annoying one

        vk::PipelineRenderingCreateInfo renderingInfo;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat_;
        renderingInfo.depthAttachmentFormat = depthFormat_;

        constexpr vk::VertexInputBindingDescription vertexBinding{
            .binding = 0,
            .stride = sizeof(types::Vertex),
            .inputRate = vk::VertexInputRate::eVertex,
        };

        constexpr vk::VertexInputAttributeDescription vertexAttributes[]{
            {
                .location = 0,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(types::Vertex, position),
            },
            {
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(types::Vertex, uv),
            },
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputState{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vertexBinding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(vertexAttributes)),
            .pVertexAttributeDescriptions = vertexAttributes,
        };

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
        inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssemblyState.primitiveRestartEnable = false;

        vk::PipelineViewportStateCreateInfo viewportState;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizationState;
        rasterizationState.depthClampEnable = false;
        rasterizationState.rasterizerDiscardEnable = false;
        rasterizationState.polygonMode = vk::PolygonMode::eFill;
        rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizationState.depthBiasEnable = false;
        rasterizationState.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo multisampleState;
        multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencilState;
        depthStencilState.depthTestEnable = true;
        depthStencilState.depthWriteEnable = true;
        depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencilState.depthBoundsTestEnable = false;
        depthStencilState.minDepthBounds = 0.0f;
        depthStencilState.maxDepthBounds = 1.0f;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.blendEnable = false;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlendState;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &colorBlendAttachment;

        vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        vk::GraphicsPipelineCreateInfo createInfo;
        createInfo.pNext = &renderingInfo;

        createInfo.stageCount = shaderStages.size();
        createInfo.pStages = shaderStages.data();
        createInfo.pVertexInputState = &vertexInputState;
        createInfo.pInputAssemblyState = &inputAssemblyState;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterizationState;
        createInfo.pMultisampleState = &multisampleState;
        createInfo.pDepthStencilState = &depthStencilState;
        createInfo.pColorBlendState = &colorBlendState;
        createInfo.pDynamicState = &dynamicState;
        createInfo.pVertexInputState = &vertexInputState;

        createInfo.layout = *pipelineLayout_;
        createInfo.renderPass = nullptr;
        createInfo.subpass = 0;

        pipeline_ = device_.getDevice().createGraphicsPipeline(
            nullptr, createInfo);

        LOG_INFO("created hello triangle graphics pipeline using dynamic rendering");
    }
}
