#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/common.hpp>

#include "assets/gltf_importer.hxx"
#include "types/texture_data.hxx"

namespace {
    void require(const bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Minimal valid 1x1 red PNG shared by every negative fixture.
    constexpr std::string_view kRedPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    [[nodiscard]] std::string redPngDataUri() {
        return "data:image/png;base64," +
            std::string{kRedPngBase64.begin(), kRedPngBase64.end()};
    }

    [[nodiscard]] std::string base64Encode(const std::vector<uint8_t> &bytes) {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        encoded.reserve((bytes.size() + 2) / 3 * 4);
        for (size_t offset = 0; offset < bytes.size(); offset += 3) {
            const uint32_t block =
                static_cast<uint32_t>(bytes[offset]) << 16 |
                (offset + 1 < bytes.size() ? static_cast<uint32_t>(bytes[offset + 1]) : 0) << 8 |
                (offset + 2 < bytes.size() ? static_cast<uint32_t>(bytes[offset + 2]) : 0);
            encoded += alphabet[block >> 18 & 0x3F];
            encoded += alphabet[block >> 12 & 0x3F];
            encoded += offset + 1 < bytes.size() ? alphabet[block >> 6 & 0x3F] : '=';
            encoded += offset + 2 < bytes.size() ? alphabet[block & 0x3F] : '=';
        }
        return encoded;
    }

    template<typename T>
    void packLE(std::vector<uint8_t> &bytes, const T &value) {
        const auto *start = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), start, start + sizeof(T));
    }

    // Shared minimal scene geometry: a triangle with positions, UVs and
    // indices packed into one data-URI buffer (positions 36 B, uvs 24 B,
    // indices 12 B = 72 B total). Extension slots append more sections.
    [[nodiscard]] std::string triangleBufferBase64() {
        constexpr float positions[9]{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        constexpr float uvs[6]{0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr uint32_t indices[3]{0, 1, 2};
        std::vector<uint8_t> bytes;
        bytes.reserve(72);
        for (const float component : positions) packLE(bytes, component);
        for (const float component : uvs) packLE(bytes, component);
        for (const uint32_t component : indices) packLE(bytes, component);
        return base64Encode(bytes);
    }

    // Base buffer plus animation timeline data:
    // times (2 x f32, 8 B) and two keyframe translations (2 x VEC3 f32, 24 B).
    [[nodiscard]] std::string animatedBufferBase64() {
        std::vector<uint8_t> bytes;
        bytes.reserve(104);
        for (const float component : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})
            packLE(bytes, component);
        for (const float component : {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f})
            packLE(bytes, component);
        for (const uint32_t component : {0u, 1u, 2u})
            packLE(bytes, component);
        for (const float component : {0.0f, 1.0f}) packLE(bytes, component);
        for (const float component : {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f})
            packLE(bytes, component);
        return base64Encode(bytes);
    }

    // Skinned-fixture buffer: positions (36 B), indices (12 B), joints (24 B),
    // weights (24 B) and one identity inverse-bind matrix (64 B) = 160 B.
    [[nodiscard]] std::string skinBufferBase64() {
        std::vector<uint8_t> bytes;
        bytes.reserve(160);
        for (const float component : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})
            packLE(bytes, component);                 // positions
        for (const uint32_t component : {0u, 1u, 2u})
            packLE(bytes, component);                 // indices
        for (const uint16_t component : {0u, 0u, 0u, 0u})
            packLE(bytes, component);                 // joint 0
        for (const uint16_t component : {1u, 0u, 0u, 0u})
            packLE(bytes, component);                 // joint 1
        for (const uint16_t component : {0u, 0u, 0u, 0u})
            packLE(bytes, component);                 // joint 2
        for (const uint16_t component : {0x3FFFu, 0u, 0u, 0u,
                                         0x3FFFu, 0u, 0u, 0u,
                                         0x3FFFu, 0u, 0u, 0u})
            packLE(bytes, component);                 // weights, one per joint slot
        for (const float row : {1.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, 1.0f})
            packLE(bytes, row);                       // inverse-bind matrices
        return base64Encode(bytes);
    }

    // Fixture skeleton for the negative cases; every slot receives a JSON
    // fragment (or empty string) that the case wants to override. Accessor
    // and buffer-view slots append entries after the standard three.
    struct FixtureSlots {
        std::string nodes;         // after "mesh": 0, inside the node object
        std::string primitives;    // primitives array entry body
        std::string materials;     // materials array entry body
        std::string images;        // images array entry body
        std::string textures;      // textures array entry body
        std::string bufferViews;   // appended buffer-view entries
        std::string accessors;     // appended accessor entries
        std::string firstView;     // suffix inside buffer view 0
        std::string extras;        // appended with a comma at document scope
        std::string bufferLength = "72";
        std::string bufferBase64 = triangleBufferBase64();
    };

    [[nodiscard]] std::string negativeFixture(const FixtureSlots &slots) {
        std::string fixture = R"({
            "asset": {"version": "2.0", "generator": "importer negative fixture"},
            "scene": 0,
            "scenes": [{"name": "Negative import test", "nodes": [0]}],
            "nodes": [{"name": "Solo node", "mesh": 0)";
        fixture += slots.nodes;
        fixture += "}],\n";
        fixture += R"("meshes": [{
                "name": "Solo mesh",
                "primitives": [{)";
        fixture += slots.primitives;
        fixture += "}]}],\n";
        fixture += R"("accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                 "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
                {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2",
                 "min": [0.0, 0.0], "max": [1.0, 1.0]},
                {"bufferView": 2, "componentType": 5125, "count": 3, "type": "SCALAR"})";
        if (!slots.accessors.empty())
            fixture += "," + slots.accessors;
        fixture += R"(],"bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962)";
        fixture += slots.firstView;
        fixture += R"(},{"buffer": 0, "byteOffset": 36, "byteLength": 24, "target": 34962},
                {"buffer": 0, "byteOffset": 60, "byteLength": 12, "target": 34963})";
        if (!slots.bufferViews.empty())
            fixture += "," + slots.bufferViews;
        fixture += R"(],"buffers": [{"byteLength": )";
        fixture += slots.bufferLength;
        fixture += R"(,"uri": "data:application/octet-stream;base64,)";
        fixture += slots.bufferBase64;
        fixture += "\"}],\n";
        fixture += R"("materials": [{)";
        fixture += slots.materials;
        fixture += "}],\n";
        fixture += R"("textures": [{)";
        fixture += slots.textures;
        fixture += "}],\n";
        fixture += R"("samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
            "images": [{)";
        fixture += slots.images;
        fixture += "}]\n";
        if (!slots.extras.empty())
            fixture += "," + slots.extras;
        fixture += "\n        }";
        return fixture;
    }

    void expectFailure(const std::filesystem::path &directory,
                       const std::string &name,
                       const std::string &fixture,
                       const std::string_view expectedMessagePart,
                       const york::assets::GltfImporter &importer,
                       const float aspectRatio) {
        const std::filesystem::path path = directory / (name + ".gltf");
        std::ofstream file{path, std::ios::binary};
        require(file.is_open(), "negative fixture could not be written");
        file << fixture;
        file.close();

        bool rejected = false;
        try {
            (void)importer.load(path, aspectRatio);
        } catch (const std::exception &error) {
            rejected = true;
            require(std::string_view{error.what()}.find(expectedMessagePart) != std::string_view::npos,
                    ("unexpected importer rejection for " + name + ": " + error.what()).c_str());
        }
        require(rejected, ("importer accepted an unsupported scene: " + name).c_str());
    }
}

int main(const int argc, char *argv[]) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected a test case name and glTF fixture path");

        const york::assets::GltfImporter importer;
        const std::string testCase = argv[1];
        const std::filesystem::path fixturePath{argv[2]};

        // The negative suite writes crafted scenes into a scratch directory
        // and expects the importer to reject each with its documented error.
        if (testCase == "negative") {
            const float sceneStride = 800.0f / 600.0f;
            const std::filesystem::path scratch =
                std::filesystem::temp_directory_path() / "york-importer-negative";
            std::filesystem::create_directories(scratch);

            const FixtureSlots standard{
                .primitives = R"("attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2, "material": 0)",
                .materials = R"("name": "Textured opaque material",
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}})",
                .images = R"("mimeType": "image/png", "uri": ")" + redPngDataUri() + R"(")",
                .textures = R"("source": 0, "sampler": 0)",
            };

            // TEXCOORD_1 on the attribute set: only TEXCOORD_0 is imported.
            FixtureSlots texcoord1 = standard;
            texcoord1.primitives =
                R"("attributes": {"POSITION": 0, "TEXCOORD_1": 1}, "indices": 2, "material": 0)";
            expectFailure(scratch, "texcoord1", negativeFixture(texcoord1),
                "TEXCOORD_", importer, sceneStride);

            // Non-opaque blend mode is deferred to a later alpha slice.
            FixtureSlots nonopaque = standard;
            nonopaque.materials =
                R"("name": "Blend material", "alphaMode": "BLEND",
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}})";
            expectFailure(scratch, "nonopaque", negativeFixture(nonopaque),
                "alpha/unlit modes", importer, sceneStride);

            // Unlit materials piggyback on the opaque path later.
            FixtureSlots unlit = standard;
            unlit.materials =
                R"("name": "Unlit material", "extensions": {"KHR_materials_unlit": {}},
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}})";
            expectFailure(scratch, "unlit", negativeFixture(unlit),
                "alpha/unlit modes", importer, sceneStride);

            // KHR_texture_transform remaps the UV set.
            FixtureSlots transform = standard;
            transform.materials =
                R"("name": "Transformed material",
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0,
                        "extensions": {"KHR_texture_transform": {"offset": [0.25, 0.25]}}}})";
            transform.extras = R"("extensionsUsed": ["KHR_texture_transform"])";
            expectFailure(scratch, "transform", negativeFixture(transform),
                "KHR_texture_transform", importer, sceneStride);

            // Basis universal textures come before any image decoding.
            FixtureSlots basisu = standard;
            basisu.images = R"("mimeType": "image/ktx2", "uri": "data:image/ktx2;base64,AAAA")";
            basisu.textures =
                R"("source": 0, "sampler": 0, "extensions": {"KHR_texture_basisu": {"source": 0}})";
            basisu.extras = R"("extensionsUsed": ["KHR_texture_basisu"])";
            expectFailure(scratch, "basisu", negativeFixture(basisu),
                "basisu/webp image extensions", importer, sceneStride);

            // Skinned nodes belong to the animation slice.
            FixtureSlots skinned = standard;
            skinned.primitives =
                R"("attributes": {"POSITION": 0, "JOINTS_0": 3, "WEIGHTS_0": 4},
                    "indices": 2, "material": 0)";
            skinned.nodes = R"(, "skin": 0)";
            skinned.bufferLength = "160";
            skinned.bufferBase64 = skinBufferBase64();
            skinned.bufferViews =
                R"({"buffer": 0, "byteOffset": 48, "byteLength": 24, "target": 34962},
                   {"buffer": 0, "byteOffset": 72, "byteLength": 24, "target": 34962},
                   {"buffer": 0, "byteOffset": 96, "byteLength": 64})";
            skinned.accessors =
                R"({"bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4"},
                   {"bufferView": 4, "componentType": 5123, "count": 3, "type": "VEC4", "normalized": true},
                   {"bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4"})";
            skinned.extras = R"("skins": [{"inverseBindMatrices": 5,
                                            "skeleton": 0, "joints": [0]}])";
            expectFailure(scratch, "skinned", negativeFixture(skinned),
                "skinned glTF nodes", importer, sceneStride);

            // Animations remain unsupported in the static importer.
            FixtureSlots animated = standard;
            animated.bufferLength = "104";
            animated.bufferBase64 = animatedBufferBase64();
            animated.bufferViews =
                R"({"buffer": 0, "byteOffset": 72, "byteLength": 8},
                   {"buffer": 0, "byteOffset": 80, "byteLength": 24})";
            animated.accessors =
                R"({"bufferView": 3, "componentType": 5126, "count": 2, "type": "SCALAR",
                    "min": [0.0], "max": [1.0]},
                   {"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC3"})";
            animated.extras =
                R"("animations": [{"name": "Squash",
                                   "samplers": [{"input": 3, "output": 4}],
                                   "channels": [{"sampler": 0,
                                                 "target": {"node": 0, "path": "translation"}}]}])";
            expectFailure(scratch, "animated", negativeFixture(animated),
                "animated glTF scenes", importer, sceneStride);

            // EXT_meshopt_compression on the position buffer view.
            FixtureSlots meshopt = standard;
            meshopt.firstView =
                R"(, "extensions": {"EXT_meshopt_compression": {"buffer": 0, "byteOffset": 0,
                     "byteLength": 36, "byteStride": 12, "count": 3, "mode": "ATTRIBUTES"}})";
            meshopt.extras = R"("extensionsUsed": ["EXT_meshopt_compression"])";
            expectFailure(scratch, "meshopt", negativeFixture(meshopt),
                "EXT_meshopt_compression", importer, sceneStride);

            // Line-list primitives are not drawable in this slice.
            FixtureSlots lines = standard;
            lines.primitives =
                R"("mode": 1, "attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2, "material": 0)";
            expectFailure(scratch, "non-triangle", negativeFixture(lines),
                "only glTF triangle-list", importer, sceneStride);

            return 0;
        }

        const float sceneStride = 800.0f / 600.0f;
        const auto scene = importer.load(fixturePath, sceneStride);
        if (testCase == "camera") {
            require(scene.name == "Camera import test", "selected glTF scene name was not imported");
            require(scene.meshes.empty() && scene.objects.empty(),
                    "camera-only scene unexpectedly contains geometry");
            require(scene.cameras.size() == 1 && scene.activeCameraIndex == 0,
                    "glTF camera was not selected as the active camera");
            require(scene.cameras[0].view[3][2] == -5.0f,
                    "glTF camera node world transform was not inverted for the view matrix");
            require(scene.cameras[0].projection[0][0] > 0.0f,
                    "glTF camera perspective projection was not imported");
            return 0;
        }

        if (testCase == "textured") {
            require(scene.name == "Textured import test", "textured scene name was not imported");
            require(scene.images.size() == 1 && scene.textures.size() == 1,
                    "textured scene image/texture relations were not imported");
            const auto &image = scene.images[0];
            require(image.mimeType == "image/png", "glTF image MIME type was not retained");
            require(image.width == 2 && image.height == 2, "glTF image dimensions were not decoded");
            require(image.rgbaPixels.size() == 2 * 2 * 4, "decoded glTF RGBA size is wrong");
            const std::vector<uint8_t> expectedPixels{
                255, 0, 0, 255,       // red
                0, 255, 0, 255,       // green
                0, 0, 255, 255,       // blue
                255, 255, 255, 255,   // white
            };
            require(image.rgbaPixels == expectedPixels,
                    "glTF image pixels were not decoded to RGBA");
            require(scene.samplers.size() == 1, "glTF sampler was not imported");
            require(scene.samplers[0].magFilter == york::types::TextureFilter::Linear &&
                    scene.samplers[0].minFilter == york::types::TextureFilter::LinearMipmapLinear,
                    "glTF sampler filters were not mapped");
            require(scene.samplers[0].wrapS == york::types::TextureWrap::Repeat &&
                    scene.samplers[0].wrapT == york::types::TextureWrap::MirroredRepeat,
                    "glTF sampler wraps were not mapped");
            require(scene.materials.size() == 2, "glTF materials were not imported");
            require(scene.materials[0].baseColorTextureIndex &&
                    *scene.materials[0].baseColorTextureIndex == 0,
                    "textured glTF material did not reference the checker texture");
            require(scene.materials[0].baseColorFactor == glm::vec4(0.5f, 0.75f, 1.0f, 1.0f),
                    "glTF base color factor was not retained");
            require(!scene.materials[1].baseColorTextureIndex,
                    "untextured glTF material unexpectedly references a texture");
            require(scene.meshes.size() == 1 && scene.meshes[0].vertices.size() == 8,
                    "textured mesh vertices were not imported");
            require(scene.meshes[0].indices ==
                        std::vector<uint32_t>{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7},
                    "shared accessor vertices were not rebased per primitive");
            require(scene.meshes[0].primitives.size() == 2,
                    "textured mesh primitives were not imported");
            require(scene.meshes[0].primitives[0].firstIndex == 0 &&
                    scene.meshes[0].primitives[0].indexCount == 6,
                    "textured primitive index range was not preserved");
            require(scene.meshes[0].primitives[1].firstIndex == 6 &&
                    scene.meshes[0].primitives[1].indexCount == 6,
                    "untextured primitive index range was not preserved");
            require(scene.meshes[0].primitives[0].materialIndex == 0,
                    "textured primitive did not keep its scene material index");
            require(!scene.meshes[0].primitives[1].materialIndex,
                    "materialless primitive unexpectedly references a material");
            require(scene.meshes[0].vertices[2].uv == glm::vec2(1.0f, 0.0f),
                    "TEXCOORD_0 values were not imported");
            require(scene.meshes[0].vertices[4].uv == glm::vec2(0.0f, 0.0f),
                    "missing TEXCOORD_0 did not receive the documented default");
            require(scene.objects.size() == 1 &&
                    scene.objects[0].worldTransform[3][2] == -2.0f,
                    "mesh node translation was not imported");
            require(scene.cameras.size() == 1 && scene.activeCameraIndex == 0,
                    "textured scene camera was not imported");
            require(scene.cameras[0].view[3][2] == -3.0f,
                    "child camera node composed neither parent nor own translation");
        } else {
            require(testCase == "mesh", "unknown importer test case");
            require(scene.name == "Primitive and instance import test", "selected glTF scene name was not imported");
            require(scene.materials.size() == 1 &&
                    scene.materials[0].name == "Primitive material association",
                    "glTF material metadata was not retained");
            require(scene.meshes.size() == 1, "shared glTF mesh was imported more than once");
            require(scene.objects.size() == 2, "glTF node instances were not imported");
            require(scene.cameras.size() == 1 && scene.activeCameraIndex == 0,
                    "missing-camera scene did not receive a default active camera");

            const auto &mesh = scene.meshes.front();
            require(mesh.vertices.size() == 12, "primitive vertex arrays were not concatenated");
            require(mesh.indices == std::vector<uint32_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
                    "index widths or primitive-local vertex offsets were imported incorrectly");
            require(mesh.primitives.size() == 4, "not all glTF primitives were imported");
            for (size_t index = 0; index < mesh.primitives.size(); ++index) {
                require(mesh.primitives[index].firstIndex == index * 3,
                        "primitive index ranges were not preserved");
                require(mesh.primitives[index].indexCount == 3,
                        "primitive index count was not preserved");
            }
            require(mesh.primitives[0].materialIndex == 0,
                    "glTF primitive material association was not preserved");
            require(!mesh.primitives[1].materialIndex,
                    "missing glTF material should remain unset");
            require(mesh.vertices.front().normal == glm::vec3{0.0f, 0.0f, 1.0f},
                    "missing normals did not receive the documented default");
            require(mesh.vertices.front().uv == glm::vec2{0.0f},
                    "missing UVs did not receive the documented default");
            require(mesh.vertices.front().tangent == glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
                    "missing tangents did not receive the documented default");
            require(scene.objects[0].meshIndex == scene.objects[1].meshIndex,
                    "two nodes do not reference the same imported mesh");
            require(scene.objects[1].worldTransform[3][0] == 3.0f,
                    "node translation was not retained in its world transform");
            require(scene.cameras[0].projection[0][0] > 0.0f,
                    "generated camera projection is not usable");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
