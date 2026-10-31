#ifndef YORK_VISUAL_SCENE_HXX
#define YORK_VISUAL_SCENE_HXX

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "render/texture_image.hxx"
#include "render/visual_object.hxx"
#include "types/mesh.hxx"
#include "types/mesh_data.hxx"
#include "types/scene_data.hxx"
#include "types/texture_data.hxx"

namespace york::render {
    class RenderPipeline;

    struct CameraId {
        uint64_t value{};
        auto operator<=>(const CameraId &) const = default;
    };

    class VisualCamera {
    public:
        VisualCamera(CameraId id, const glm::mat4 &view,
                     const glm::mat4 &projection, std::string name)
            : id_{id}, view{view}, projection{projection}, name{std::move(name)} {}

        [[nodiscard]] inline CameraId id() const
            { return id_; }

        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        std::string name;

    private:
        CameraId id_;
    };

    class VisualScene {
    public:
        explicit VisualScene(Device &device);
        VisualScene(const VisualScene &) = delete;
        VisualScene &operator=(const VisualScene &) = delete;
        VisualScene(VisualScene &&) = delete;
        VisualScene &operator=(VisualScene &&) = delete;

        [[nodiscard]] MeshId addMesh(const types::MeshData &data);
        // Uploads each CPU mesh once, then creates instances and cameras referring to those IDs.
        [[nodiscard]] std::vector<MeshId> addSceneData(const types::SceneData &data);
        [[nodiscard]] ObjectId addObject(MeshId meshId,
                                         const glm::mat4 &worldTransform = glm::mat4{1.0f},
                                         std::string name = {});
        [[nodiscard]] CameraId addCamera(const glm::mat4 &view = glm::mat4{1.0f},
                                         const glm::mat4 &projection = glm::mat4{1.0f},
                                         std::string name = {});

        // Uploads the scene data's images once each and creates its sampler
        // resources. Image data is decoded CPU-side by the importer already.
        void addSceneTextures(const types::SceneData &data);

        // Allocates one material descriptor set per already-added material
        // plus the white-fallback set for material-less primitives, from a
        // pool the scene owns. Call after addSceneTextures and addSceneData;
        // the sets render against the pipeline's material layout (set 1).
        void createMaterialDescriptorSets(const RenderPipeline &pipeline);

        [[nodiscard]] std::span<const std::unique_ptr<TextureImage>> images() const
            { return images_; }
        // Index 0 is always the fallback sampler; scene samplers start at 1.
        [[nodiscard]] std::span<const vk::raii::Sampler> samplers() const
            { return samplers_; }
        // A texture binding addresses the images()/samplers() arrays above:
        // samplerIndex 0 means the fallback sampler, scenes' own samplers add 1.
        [[nodiscard]] std::span<const types::TextureBindingData> textureBindings() const
            { return textureBindings_; }
        [[nodiscard]] std::span<const types::SceneMaterialData> materials() const
            { return materials_; }
        // Shader-visible descriptor set per material index (always valid: a
        // material that samples no texture gets the white fallback image) plus
        // fallbackDescriptorSet() for primitives with no material.
        [[nodiscard]] const std::vector<vk::raii::DescriptorSet> &materialDescriptorSets() const
            { return materialDescriptorSets_; }
        [[nodiscard]] const vk::DescriptorSet &fallbackDescriptorSet() const
            { return *fallbackDescriptorSet_; }
        void setActiveCamera(CameraId cameraId);

        [[nodiscard]] const types::Mesh &mesh(MeshId meshId) const;
        [[nodiscard]] VisualObject &object(ObjectId objectId);
        [[nodiscard]] const VisualObject &object(ObjectId objectId) const;
        [[nodiscard]] VisualCamera &camera(CameraId cameraId);
        [[nodiscard]] const VisualCamera &camera(CameraId cameraId) const;
        [[nodiscard]] const VisualCamera *activeCamera() const;

        [[nodiscard]] std::span<VisualObject> objects()
            { return objects_; }
        [[nodiscard]] std::span<const VisualObject> objects() const
            { return objects_; }

    private:
        [[nodiscard]] MeshId addMesh(const types::MeshData &data,
                                     uint32_t materialIndexOffset);
        void createFallbackWhiteTexture();

        Device &device_;
        std::map<MeshId, types::Mesh> meshes_;
        std::vector<std::unique_ptr<TextureImage>> images_;
        std::vector<vk::raii::Sampler> samplers_;
        std::vector<vk::raii::DescriptorPool> descriptorPools_;
        std::vector<vk::raii::DescriptorSet> materialDescriptorSets_;
        // Shader-visible set for primitives with no material (samples the
        // white fallback image). Material sets are always valid themselves.
        vk::raii::DescriptorSet fallbackDescriptorSet_{nullptr};
        std::vector<types::TextureBindingData> textureBindings_;
        std::vector<types::SceneMaterialData> materials_;
        std::vector<VisualObject> objects_;
        std::map<CameraId, VisualCamera> cameras_;
        std::optional<CameraId> activeCamera_;
        uint64_t nextMeshId_{1};
        uint64_t nextObjectId_{1};
        uint64_t nextCameraId_{1};
    };
}

#endif // YORK_VISUAL_SCENE_HXX
