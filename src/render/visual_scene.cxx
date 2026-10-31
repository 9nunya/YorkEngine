#include "render/visual_scene.hxx"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "render/device.hxx"
#include "render/render_pipeline.hxx"
#include "types/texture_data.hxx"
#include "util/logger.hxx"

namespace york::render {
    VisualScene::VisualScene(Device &device)
        : device_{device} {
        // Samplers index 0 is the fallback the scene reserves for itself;
        // imported scene samplers append after it (see addSceneTextures).
        samplers_.emplace_back(device_.getDevice(), vk::SamplerCreateInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .maxAnisotropy = 1.0f,
            .minLod = 0.0f,
            .maxLod = 0.0f,
        });
        createFallbackWhiteTexture();
    }

    void VisualScene::createFallbackWhiteTexture() {
        types::ImageData whiteImage{
            .name = "Fallback white",
            .mimeType = "image/raw-rgba",
            .width = 1,
            .height = 1,
        };
        whiteImage.rgbaPixels = {255, 255, 255, 255};
        images_.push_back(std::make_unique<TextureImage>(device_, whiteImage));
    }

    void VisualScene::addSceneTextures(const types::SceneData &data) {
        // Each image uploads exactly once per load request: repeated
        // addSceneTextures calls would recreate them. Loading a mesh once and
        // reusing MeshIds matches the layout contract in the design notes.
        const auto existingImageCount = images_.size();
        for (const auto &image : data.images)
            images_.push_back(std::make_unique<TextureImage>(device_, image));

        for (const auto &sampler : data.samplers) {
            const auto mapFilter = [](const types::TextureFilter filter) {
                switch (filter) {
                    case types::TextureFilter::Nearest:
                    case types::TextureFilter::NearestMipmapNearest:
                    case types::TextureFilter::NearestMipmapLinear:
                        return vk::Filter::eNearest;
                    case types::TextureFilter::Linear:
                    case types::TextureFilter::LinearMipmapNearest:
                    case types::TextureFilter::LinearMipmapLinear:
                        return vk::Filter::eLinear;
                }
                throw std::runtime_error("unsupported texture filter");
            };
            // Textures upload a single mip, so the mip mode only has to be a
            // valid value; linearity picks the mode the glTF min filter uses.
            const auto mapMipMode = [](const types::TextureFilter filter) {
                return filter == types::TextureFilter::NearestMipmapNearest ||
                       filter == types::TextureFilter::LinearMipmapNearest
                    ? vk::SamplerMipmapMode::eNearest
                    : vk::SamplerMipmapMode::eLinear;
            };
            const auto mapWrap = [](const types::TextureWrap wrap) {
                switch (wrap) {
                    case types::TextureWrap::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
                    case types::TextureWrap::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
                    case types::TextureWrap::Repeat: return vk::SamplerAddressMode::eRepeat;
                }
                throw std::runtime_error("unsupported texture wrap mode");
            };
            const uint32_t baseMipLod = 0;
            samplers_.emplace_back(device_.getDevice(), vk::SamplerCreateInfo{
                .magFilter = mapFilter(sampler.magFilter),
                .minFilter = mapFilter(sampler.minFilter),
                .mipmapMode = mapMipMode(sampler.minFilter),
                .addressModeU = mapWrap(sampler.wrapS),
                .addressModeV = mapWrap(sampler.wrapT),
                .addressModeW = mapWrap(sampler.wrapS),
                .mipLodBias = 0.0f,
                .anisotropyEnable = false,
                .maxAnisotropy = 1.0f,
                .compareEnable = false,
                .compareOp = vk::CompareOp::eNever,
                .minLod = static_cast<float>(baseMipLod),
                .maxLod = static_cast<float>(baseMipLod),
                .borderColor = vk::BorderColor::eFloatOpaqueBlack,
                .unnormalizedCoordinates = false,
            });
        }

        const size_t imageOffset = existingImageCount;

        // Scene texture j addresses image imageOffset + its scene image and,
        // when the glTF texture names an explicit sampler, sampler 1 + that
        // index; otherwise it rides on the scene's fallback sampler at 0.
        for (const auto &texture : data.textures)
            textureBindings_.push_back({
                .imageIndex = static_cast<uint32_t>(imageOffset + texture.imageIndex),
                .samplerIndex = texture.samplerIndex
                    ? 1 + *texture.samplerIndex
                    : 0,
            });
    }

    void VisualScene::createMaterialDescriptorSets(const RenderPipeline &pipeline) {
        if (!materialDescriptorSets_.empty())
            throw std::logic_error("material descriptor sets already exist for this scene");

        // One set per material plus the material-less primitive fallback;
        // each set holds one combined image sampler.
        const size_t setCount = materials_.size() + 1;
        const vk::DescriptorPoolSize samplerPoolSize{
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = static_cast<uint32_t>(setCount),
        };
        descriptorPools_.push_back(device_.getDevice().createDescriptorPool(
            vk::DescriptorPoolCreateInfo{
                .maxSets = static_cast<uint32_t>(setCount),
                .poolSizeCount = 1,
                .pPoolSizes = &samplerPoolSize,
            }));

        std::vector<vk::DescriptorSetLayout> layouts(setCount, *pipeline.getMaterialDescriptorSetLayout());
        vk::raii::DescriptorSets sets = device_.getDevice().allocateDescriptorSets(
            vk::DescriptorSetAllocateInfo{
                .descriptorPool = *descriptorPools_.back(),
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });

        // The fallback rides the last allocated slot so the set indices stay
        // aligned with scene material indices.
        fallbackDescriptorSet_ = std::move(sets.back());
        sets.pop_back();
        materialDescriptorSets_ = std::move(sets);

        const auto writeSampler = [this](const vk::DescriptorSet &set,
                                         const vk::Sampler sampler,
                                         const vk::ImageView view) {
            const vk::DescriptorImageInfo imageInfo{
                .sampler = sampler,
                .imageView = view,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            };
            const vk::WriteDescriptorSet write{
                .dstSet = set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfo,
            };
            device_.getDevice().updateDescriptorSets(write, {});
        };

        // Untextured materials and material-less primitives bind the fallback:
        // the shader always samples something valid, never a dangling nullptr.
        const vk::Sampler fallbackSampler = *samplers_[0];
        const vk::ImageView fallbackView = images_[0]->view();
        writeSampler(*fallbackDescriptorSet_, fallbackSampler, fallbackView);
        for (size_t material = 0; material < materials_.size(); ++material) {
            const auto &materialData = materials_[material];
            if (materialData.baseColorTextureIndex) {
                const types::TextureBindingData &binding =
                    textureBindings_.at(*materialData.baseColorTextureIndex);
                writeSampler(
                    *materialDescriptorSets_[material],
                    *samplers_.at(binding.samplerIndex),
                    images_.at(binding.imageIndex)->view());
            } else {
                writeSampler(*materialDescriptorSets_[material], fallbackSampler, fallbackView);
            }
        }
    }

    MeshId VisualScene::addMesh(const types::MeshData &data) {
        return addMesh(data, 0);
    }

    MeshId VisualScene::addMesh(const types::MeshData &data,
                                const uint32_t materialIndexOffset) {
        const MeshId meshId{nextMeshId_++};
        meshes_.emplace(std::piecewise_construct,
                        std::forward_as_tuple(meshId),
                        std::forward_as_tuple(data, device_, materialIndexOffset));
        return meshId;
    }

    std::vector<MeshId> VisualScene::addSceneData(const types::SceneData &data) {
        for (const auto &objectData : data.objects)
            if (objectData.meshIndex >= data.meshes.size())
                throw std::out_of_range("scene object refers to a missing CPU mesh");
        if (data.activeCameraIndex && *data.activeCameraIndex >= data.cameras.size())
            throw std::out_of_range("scene active camera refers to a missing CPU camera");
        const size_t maxMaterialCount = std::numeric_limits<uint32_t>::max();
        if (materials_.size() > maxMaterialCount ||
            data.materials.size() > maxMaterialCount - materials_.size())
            throw std::length_error("visual scene has too many materials");
        for (const auto &meshData : data.meshes)
            for (const auto &primitive : meshData.primitives)
                if (primitive.materialIndex && *primitive.materialIndex >= data.materials.size())
                    throw std::out_of_range("mesh primitive refers to a missing CPU material");
        const uint32_t materialIndexOffset = static_cast<uint32_t>(materials_.size());

        std::vector<MeshId> meshIds;
        meshIds.reserve(data.meshes.size());
        for (const auto &meshData : data.meshes)
            meshIds.push_back(addMesh(meshData, materialIndexOffset));

        materials_.insert(materials_.end(), data.materials.begin(), data.materials.end());

        for (const auto &objectData : data.objects)
            (void)addObject(meshIds.at(objectData.meshIndex),
                            objectData.worldTransform,
                            objectData.name);
        std::vector<CameraId> cameraIds;
        cameraIds.reserve(data.cameras.size());
        for (const auto &cameraData : data.cameras)
            cameraIds.push_back(addCamera(
                cameraData.view, cameraData.projection, cameraData.name));
        if (data.activeCameraIndex)
            setActiveCamera(cameraIds.at(*data.activeCameraIndex));

        return meshIds;
    }

    ObjectId VisualScene::addObject(const MeshId meshId,
                                    const glm::mat4 &worldTransform,
                                    std::string name) {
        if (!meshes_.contains(meshId))
            throw std::out_of_range("visual object refers to a mesh not owned by this scene");

        const ObjectId objectId{nextObjectId_++};
        objects_.emplace_back(objectId, meshId, worldTransform, std::move(name));
        return objectId;
    }

    CameraId VisualScene::addCamera(const glm::mat4 &view,
                                    const glm::mat4 &projection,
                                    std::string name) {
        const CameraId cameraId{nextCameraId_++};
        cameras_.emplace(cameraId, VisualCamera{cameraId, view, projection, std::move(name)});
        if (!activeCamera_)
            activeCamera_ = cameraId;
        return cameraId;
    }

    void VisualScene::setActiveCamera(const CameraId cameraId) {
        if (!cameras_.contains(cameraId))
            throw std::out_of_range("active camera is not owned by this scene");
        activeCamera_ = cameraId;
    }

    const types::Mesh &VisualScene::mesh(const MeshId meshId) const {
        return meshes_.at(meshId);
    }

    VisualObject &VisualScene::object(const ObjectId objectId) {
        const auto found = std::ranges::find(objects_, objectId, &VisualObject::id);
        if (found == objects_.end())
            throw std::out_of_range("visual object ID is not present in this scene");
        return *found;
    }

    const VisualObject &VisualScene::object(const ObjectId objectId) const {
        const auto found = std::ranges::find(objects_, objectId, &VisualObject::id);
        if (found == objects_.end())
            throw std::out_of_range("visual object ID is not present in this scene");
        return *found;
    }

    VisualCamera &VisualScene::camera(const CameraId cameraId) {
        return cameras_.at(cameraId);
    }

    const VisualCamera &VisualScene::camera(const CameraId cameraId) const {
        return cameras_.at(cameraId);
    }

    const VisualCamera *VisualScene::activeCamera() const {
        if (!activeCamera_)
            return nullptr;
        return &cameras_.at(*activeCamera_);
    }
}
