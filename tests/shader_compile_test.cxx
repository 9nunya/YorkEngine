#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "render/shader.hxx"
#include "util/fs.hxx"

namespace {
    void require(const bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    }
}

// Verifies the render pipeline's shader sources compile to SPIR-V through
// the same shaderc compiler the engine uses. Catches vertex-input, push
// constant and descriptor declaration drift before any windowed run.
int main(const int argc, char *argv[]) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected the shader source directory");

        const std::filesystem::path root = std::filesystem::path{argv[1]} / "shaders";
        const york::render::ShaderCompiler compiler;

        const std::pair<york::render::ShaderStage, std::string> cases[]{
            {york::render::ShaderStage::Vertex, "triangle.vert"},
            {york::render::ShaderStage::Fragment, "triangle.frag"},
        };

        for (const auto &[stage, name] : cases) {
            const std::string source = york::util::fs::read_file_string(root / name);
            const auto words = compiler.compile(source, stage, name);
            require(!words.empty(), ("shader did not compile: " + name).c_str());
            // SPIR-V magic number b4 02 00 -> words little-endian 0x07230203.
            require(words.front() == 0x07230203, ("compiled " + name + " is not SPIR-V").c_str());
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
