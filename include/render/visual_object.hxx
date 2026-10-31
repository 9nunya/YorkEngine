#ifndef YORK_VISUAL_OBJECT_HXX
#define YORK_VISUAL_OBJECT_HXX

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <glm/glm.hpp>

namespace york::render {
    struct MeshId {
        uint64_t value{};
        auto operator<=>(const MeshId &) const = default;
    };

    struct ObjectId {
        uint64_t value{};
        auto operator<=>(const ObjectId &) const = default;
    };

    struct MaterialId {
        uint64_t value{};
        auto operator<=>(const MaterialId &) const = default;
    };

    class VisualObject {
    public:
        VisualObject(ObjectId id, MeshId meshId,
                     const glm::mat4 &worldTransform, std::string name)
            : id_{id}, meshId_{meshId}, worldTransform_{worldTransform}, name_{std::move(name)} {}

        [[nodiscard]] inline ObjectId id() const
            { return id_; }
        [[nodiscard]] inline MeshId meshId() const
            { return meshId_; }
        [[nodiscard]] inline const glm::mat4 &worldTransform() const
            { return worldTransform_; }
        inline void setWorldTransform(const glm::mat4 &worldTransform)
            { worldTransform_ = worldTransform; }
        [[nodiscard]] inline bool visible() const
            { return visible_; }
        inline void setVisible(const bool visible)
            { visible_ = visible; }
        [[nodiscard]] inline bool enabled() const
            { return enabled_; }
        inline void setEnabled(const bool enabled)
            { enabled_ = enabled; }
        [[nodiscard]] inline const std::optional<MaterialId> &materialOverride() const
            { return materialOverride_; }
        inline void setMaterialOverride(const std::optional<MaterialId> materialOverride)
            { materialOverride_ = materialOverride; }
        [[nodiscard]] inline const std::string &name() const
            { return name_; }
        inline void setName(std::string name)
            { name_ = std::move(name); }

    private:
        ObjectId id_;
        MeshId meshId_;
        glm::mat4 worldTransform_{1.0f};
        bool visible_{true};
        bool enabled_{true};
        std::optional<MaterialId> materialOverride_{};
        std::string name_;
    };
}

#endif // YORK_VISUAL_OBJECT_HXX
