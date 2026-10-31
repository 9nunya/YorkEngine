#include <filesystem>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "assets/gltf_importer.hxx"
#include "assets/obj_importer.hxx"
#include "render/visual_engine.hxx"

int main(const int argc, char *argv[]) {
    const std::filesystem::path assetPath = argc > 1
        ? std::filesystem::path{argv[1]}
        : std::filesystem::path{"assets/textured.gltf"};
    std::string extension = assetPath.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    const york::types::SceneData importedScene = extension == ".obj"
        ? york::assets::ObjImporter{}.load(assetPath, 800.0f / 600.0f)
        : extension == ".gltf" || extension == ".glb"
        ? york::assets::GltfImporter{}.load(assetPath, 800.0f / 600.0f)
        : throw std::runtime_error("unsupported scene file extension '" + extension + "'");

    york::render::VisualEngine engine(800, 600);
    auto &pipeline = engine.getPipeline();
    pipeline.pushShader({york::render::ShaderStage::Vertex, "triangle.vert"});
    pipeline.pushShader({york::render::ShaderStage::Fragment, "triangle.frag"});
    engine.initialize();

    auto &scene = engine.createScene();
    scene.addSceneTextures(importedScene);
    (void)scene.addSceneData(importedScene);
    scene.createMaterialDescriptorSets(engine.getPipeline());

    auto &window = engine.getWindow();
    while (!window.shouldClose()) {
        york::render::Window::pollEvents();
        if (window.consumeResize())
            engine.getRenderer().recreate();
        engine.renderFrame();
    }

    return 0;
}
