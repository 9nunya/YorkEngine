#include "assets/default_camera.hxx"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace york::assets {
    void addDefaultCamera(types::SceneData &sceneData, const float aspectRatio) {
        glm::vec3 minimum{std::numeric_limits<float>::max()};
        glm::vec3 maximum{std::numeric_limits<float>::lowest()};
        bool hasVertex = false;
        for (const auto &object : sceneData.objects) {
            const auto &mesh = sceneData.meshes.at(object.meshIndex);
            for (const auto &vertex : mesh.vertices) {
                const glm::vec4 worldPosition = object.worldTransform * glm::vec4{vertex.position, 1.0f};
                const glm::vec3 position = glm::vec3{worldPosition} / worldPosition.w;
                minimum = glm::min(minimum, position);
                maximum = glm::max(maximum, position);
                hasVertex = true;
            }
        }

        const glm::vec3 center = hasVertex ? (minimum + maximum) * 0.5f : glm::vec3{0.0f};
        const float radius = hasVertex
            ? std::max(glm::length(maximum - minimum) * 0.5f, 0.5f)
            : 1.0f;
        constexpr float verticalFov = glm::radians(45.0f);
        const float distance = radius / std::sin(verticalFov * 0.5f) * 1.15f;
        const float nearPlane = std::max(0.01f, distance - radius * 1.5f);
        const float farPlane = distance + radius * 1.5f;

        sceneData.cameras.push_back({
            .view = glm::lookAtRH(center + glm::vec3{0.0f, 0.0f, distance},
                                  center,
                                  glm::vec3{0.0f, 1.0f, 0.0f}),
            .projection = glm::perspectiveRH_ZO(verticalFov, aspectRatio, nearPlane, farPlane),
            .name = "Imported default camera",
        });
        sceneData.activeCameraIndex = static_cast<uint32_t>(sceneData.cameras.size() - 1);
    }
}
