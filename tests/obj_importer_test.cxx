#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/common.hpp>

#include "assets/obj_importer.hxx"

namespace {
    void require(const bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main(const int argc, char *argv[]) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected an OBJ fixture path");

        const auto scene = york::assets::ObjImporter{}.load(argv[1]);
        require(scene.name == "obj_importer_test", "OBJ scene name was not derived from its path");
        require(scene.meshes.size() == 1 && scene.objects.size() == 1,
                "OBJ object/group was not imported as one mesh instance");
        require(scene.objects[0].name == "OBJ Import Test" && scene.objects[0].meshIndex == 0,
                "OBJ object name or mesh relationship was not retained");
        require(scene.meshes[0].vertices.size() == 4,
                "OBJ face vertices were not deduplicated by position/UV/normal tuple");
        require(scene.meshes[0].indices == std::vector<uint32_t>{0, 1, 2, 0, 2, 3, 0, 2, 3},
                "OBJ polygon triangulation or negative indices were imported incorrectly");
        require(scene.meshes[0].primitives.size() == 2,
                "OBJ usemtl transitions were not retained as separate primitives");
        require(scene.meshes[0].primitives[0].firstIndex == 0 &&
                scene.meshes[0].primitives[0].indexCount == 6 &&
                scene.meshes[0].primitives[0].materialIndex == 0,
                "first OBJ material primitive range is wrong");
        require(scene.meshes[0].primitives[1].firstIndex == 6 &&
                scene.meshes[0].primitives[1].indexCount == 3 &&
                scene.meshes[0].primitives[1].materialIndex == 1,
                "second OBJ material primitive range is wrong");
        require(scene.meshes[0].vertices[0].uv == glm::vec2(0.0f, 1.0f) &&
                scene.meshes[0].vertices[2].uv == glm::vec2(1.0f, 0.0f),
                "OBJ texture coordinate import or vertical convention is wrong");
        require(scene.meshes[0].vertices[0].normal == glm::vec3(0.0f, 0.0f, 1.0f),
                "OBJ vertex normal was not imported");
        require(scene.materials.size() == 2 && scene.materials[0].name == "Checker" &&
                scene.materials[1].name == "Plain",
                "MTL material names were not imported in library order");
        require(scene.materials[0].baseColorFactor == glm::vec4(0.5f, 0.75f, 1.0f, 1.0f),
                "MTL Kd diffuse factor was not imported");
        require(scene.materials[0].baseColorTextureIndex == 0 &&
                !scene.materials[1].baseColorTextureIndex,
                "MTL diffuse texture relation is incorrect");
        require(scene.images.size() == 1 && scene.images[0].width == 2 &&
                scene.images[0].height == 2 && scene.images[0].rgbaPixels.size() == 16,
                "MTL map_Kd image was not decoded to owned RGBA pixels");
        require(scene.textures.size() == 1 && scene.textures[0].imageIndex == 0,
                "MTL diffuse texture-to-image relation was not preserved");
        require(scene.textures[0].samplerIndex == 0 && scene.samplers.size() == 1 &&
                scene.samplers[0].wrapS == york::types::TextureWrap::ClampToEdge &&
                scene.samplers[0].wrapT == york::types::TextureWrap::ClampToEdge,
                "MTL map_Kd clamp option was not mapped to a sampler");
        require(scene.cameras.size() == 1 && scene.activeCameraIndex == 0,
                "OBJ scene did not receive a fitted default camera");

        const auto missingAttributesPath =
            std::filesystem::path{argv[1]}.parent_path() / "obj_missing_attributes_test.obj";
        const auto missingAttributesScene =
            york::assets::ObjImporter{}.load(missingAttributesPath);
        require(missingAttributesScene.meshes.size() == 1 &&
                missingAttributesScene.meshes[0].vertices.size() == 3,
                "OBJ face without optional attributes was not imported");
        require(missingAttributesScene.meshes[0].indices == std::vector<uint32_t>{0, 1, 2},
                "negative OBJ position indices were not resolved against preceding vertices");
        require(missingAttributesScene.meshes[0].vertices[0].uv == glm::vec2{0.0f},
                "missing OBJ texture coordinates did not receive neutral UVs");
        require(missingAttributesScene.meshes[0].vertices[0].normal == glm::vec3(0.0f, 0.0f, 1.0f),
                "missing OBJ normals were not generated from the face");
        require(!missingAttributesScene.meshes[0].primitives[0].materialIndex &&
                missingAttributesScene.materials.empty(),
                "material-less OBJ primitive unexpectedly has a material");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
