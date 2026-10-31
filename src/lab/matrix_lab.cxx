#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_clip_space.hpp>

namespace {
    bool near(float a, float b, float eps = 1e-4f) {
        return std::fabs(a - b) < eps;
    }

    bool near(glm::vec3 a, glm::vec3 b, float eps = 1e-4f) {
        return glm::length(a - b) < eps;
    }

    void printMatrix(std::string_view name, const glm::mat4 &m) {
        std::printf("%s =\n", name.data());
        const float *raw = glm::value_ptr(m);
        for (int row = 0; row < 4; ++row) {
            std::printf("  %8.3f %8.3f %8.3f %8.3f\n",
                        raw[0 * 4 + row], raw[1 * 4 + row],
                        raw[2 * 4 + row], raw[3 * 4 + row]);
        }
    }
}

int main() {
    // identity leaves a point unchanged
    static_assert(
        glm::mat4(1.0f) * glm::vec4(1,2,3,1)
        == glm::vec4(1,2,3,1),
        "math fundamentals just broke");

    // identity leaves a direction unchanged
    static_assert(
        glm::mat4(1.0f) * glm::vec4(1,2,3,0)
        == glm::vec4(1,2,3,0),
        "math fundamentals just broke");

    // translation, move a point
    constexpr glm::mat4 translated = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 20.f, 30.f));

    // translation definitely moves a point
    assert(
        near(
            glm::vec3(translated * glm::vec4(0.f, 0.f, 0.f, 1.f)),
            glm::vec3(10.f, 20.f, 30.f)
            ));

    // multiplying by direction leaves it unchanged
    assert(
        translated * glm::vec4(1, 0, 0, 0)
        == glm::vec4(1, 0, 0, 0)
        );

    // 4th column is position / translation ig
    assert(
        translated[3] == glm::vec4(10.f, 20.f, 30.f, 1.f)
        );

    const glm::mat4 scaled = glm::scale(glm::mat4(1.f), glm::vec3(2.f, 3.f, 4.f));
    printMatrix("scaled", scaled);

    return 67;
}