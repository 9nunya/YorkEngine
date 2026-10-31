#ifndef YORK_VERTEX_HXX
#define YORK_VERTEX_HXX

#include <glm/glm.hpp>

namespace york::types {
    struct Vertex {
        glm::f32vec3 position;
        glm::f32vec3 normal;
        glm::f32vec2 uv;
        glm::f32vec4 tangent;

        Vertex(glm::f32vec3 position_, glm::f32vec3 normal_, glm::f32vec2 uv_, glm::f32vec4 tangent_)
            : position(position_), normal(normal_), uv(uv_), tangent(tangent_) {};
    };

    // must be 48 bits
    static_assert(sizeof(Vertex) == 48);
    static_assert(offsetof(Vertex, tangent) == 32);
}

#endif //YORK_VERTEX_HXX
