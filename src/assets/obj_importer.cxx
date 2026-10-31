#include "assets/obj_importer.hxx"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "assets/default_camera.hxx"
#include "assets/image_decode.hxx"

namespace york::assets {
    namespace {
        constexpr uint32_t kMissingIndex = std::numeric_limits<uint32_t>::max();

        struct FaceVertex {
            uint32_t positionIndex{};
            uint32_t uvIndex{kMissingIndex};
            uint32_t normalIndex{kMissingIndex};
        };

        struct VertexKey {
            uint32_t positionIndex{};
            uint32_t uvIndex{kMissingIndex};
            uint32_t normalIndex{kMissingIndex};
            uint32_t generatedNormalFace{};

            bool operator==(const VertexKey &) const = default;
        };

        struct VertexKeyHash {
            [[nodiscard]] size_t operator()(const VertexKey &key) const noexcept {
                size_t hash = key.positionIndex;
                const auto mix = [&hash](const uint32_t value) {
                    hash ^= static_cast<size_t>(value) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
                };
                mix(key.uvIndex);
                mix(key.normalIndex);
                mix(key.generatedNormalFace);
                return hash;
            }
        };

        struct MaterialRecord {
            types::SceneMaterialData data;
            std::optional<uint32_t> textureIndex;
        };

        struct MtlTexturePath {
            std::filesystem::path path;
            bool clampToEdge{};
        };

        [[nodiscard]] std::string trim(std::string value) {
            const size_t begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            const size_t end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        [[nodiscard]] std::vector<std::string> splitWords(const std::string_view text) {
            std::vector<std::string> words;
            std::string word;
            char quote = '\0';
            bool escaping = false;
            for (const char character : text) {
                if (escaping) {
                    word.push_back(character);
                    escaping = false;
                } else if (character == '\\' && quote != '\0') {
                    escaping = true;
                } else if (quote != '\0') {
                    if (character == quote)
                        quote = '\0';
                    else
                        word.push_back(character);
                } else if (character == '\'' || character == '"') {
                    quote = character;
                } else if (character == ' ' || character == '\t' || character == '\r') {
                    if (!word.empty()) {
                        words.push_back(std::move(word));
                        word.clear();
                    }
                } else {
                    word.push_back(character);
                }
            }
            if (escaping || quote != '\0')
                throw std::runtime_error("OBJ/MTL line contains an unterminated quoted value");
            if (!word.empty())
                words.push_back(std::move(word));
            return words;
        }

        [[nodiscard]] std::string lineContent(std::string line) {
            const size_t comment = line.find('#');
            if (comment != std::string::npos)
                line.resize(comment);
            return trim(std::move(line));
        }

        [[nodiscard]] float parseFloat(const std::string_view text,
                                       const std::filesystem::path &path,
                                       const size_t lineNumber) {
            float value{};
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
                throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                         ": invalid finite floating-point value '" + std::string{text} + "'");
            return value;
        }

        [[nodiscard]] int64_t parseIndex(const std::string_view text,
                                         const std::filesystem::path &path,
                                         const size_t lineNumber) {
            int64_t value{};
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || value == 0)
                throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                         ": invalid OBJ index '" + std::string{text} + "'");
            return value;
        }

        [[nodiscard]] uint32_t resolveIndex(const std::string_view text,
                                            const size_t count,
                                            const std::filesystem::path &path,
                                            const size_t lineNumber) {
            const int64_t sourceIndex = parseIndex(text, path, lineNumber);
            const int64_t resolved = sourceIndex > 0
                ? sourceIndex - 1
                : static_cast<int64_t>(count) + sourceIndex;
            if (resolved < 0 || static_cast<uint64_t>(resolved) >= count ||
                static_cast<uint64_t>(resolved) > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                         ": OBJ index is outside the data defined so far");
            return static_cast<uint32_t>(resolved);
        }

        [[nodiscard]] FaceVertex parseFaceVertex(
            const std::string_view token,
            const size_t positionCount,
            const size_t uvCount,
            const size_t normalCount,
            const std::filesystem::path &path,
            const size_t lineNumber) {
            const size_t firstSlash = token.find('/');
            const size_t secondSlash = firstSlash == std::string_view::npos
                ? std::string_view::npos
                : token.find('/', firstSlash + 1);
            const std::string_view position = token.substr(0, firstSlash);
            if (position.empty())
                throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                         ": OBJ face vertex is missing a position index");

            FaceVertex result{
                .positionIndex = resolveIndex(position, positionCount, path, lineNumber),
            };
            if (firstSlash == std::string_view::npos)
                return result;

            const std::string_view uv = token.substr(firstSlash + 1,
                (secondSlash == std::string_view::npos ? token.size() : secondSlash) - firstSlash - 1);
            if (!uv.empty())
                result.uvIndex = resolveIndex(uv, uvCount, path, lineNumber);
            if (secondSlash != std::string_view::npos) {
                const std::string_view normal = token.substr(secondSlash + 1);
                if (!normal.empty())
                    result.normalIndex = resolveIndex(normal, normalCount, path, lineNumber);
            }
            return result;
        }

        [[nodiscard]] MtlTexturePath readMtlTexturePath(
            const std::string &arguments,
            const std::filesystem::path &mtlPath) {
            const std::vector<std::string> words = splitWords(arguments);
            if (words.empty())
                throw std::runtime_error("MTL map_Kd is missing its image path in '" + mtlPath.string() + "'");

            size_t pathStart = 0;
            bool clampToEdge = false;
            while (pathStart < words.size() && words[pathStart].starts_with('-')) {
                const std::string &option = words[pathStart];
                if (option == "-clamp") {
                    if (pathStart + 1 >= words.size())
                        throw std::runtime_error("MTL map_Kd -clamp requires on or off");
                    const std::string &value = words[pathStart + 1];
                    if (value != "on" && value != "off")
                        throw std::runtime_error("MTL map_Kd -clamp value must be on or off");
                    clampToEdge = value == "on";
                    pathStart += 2;
                    continue;
                }
                // UV transform and channel options change the map's sampling
                // semantics; silently dropping them would produce wrong UVs.
                throw std::runtime_error("unsupported MTL map_Kd option '" + option + "'");
            }
            if (pathStart >= words.size())
                throw std::runtime_error("MTL map_Kd is missing its image path in '" + mtlPath.string() + "'");

            std::string filename = words[pathStart];
            for (size_t word = pathStart + 1; word < words.size(); ++word)
                filename += " " + words[word];
            const std::filesystem::path texturePath{filename};
            return {
                .path = texturePath.is_absolute() ? texturePath : mtlPath.parent_path() / texturePath,
                .clampToEdge = clampToEdge,
            };
        }

        [[nodiscard]] std::string mimeTypeForPath(const std::filesystem::path &path) {
            std::string extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (extension == ".png") return "image/png";
            if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
            if (extension == ".bmp") return "image/bmp";
            if (extension == ".tga") return "image/x-tga";
            if (extension == ".gif") return "image/gif";
            if (extension == ".psd") return "image/vnd.adobe.photoshop";
            if (extension == ".hdr") return "image/vnd.radiance";
            return "application/octet-stream";
        }

        [[nodiscard]] std::vector<uint8_t> readFileBytes(const std::filesystem::path &path) {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file)
                throw std::runtime_error("failed to open OBJ/MTL image '" + path.string() + "'");
            const std::streampos end = file.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max() ||
                static_cast<uint64_t>(end) > std::numeric_limits<std::streamsize>::max())
                throw std::runtime_error("OBJ/MTL image file is empty or too large: '" + path.string() + "'");
            std::vector<uint8_t> bytes(static_cast<size_t>(end));
            file.seekg(0, std::ios::beg);
            if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("failed to read OBJ/MTL image '" + path.string() + "'");
            return bytes;
        }

        void parseMtlFile(const std::filesystem::path &path,
                          std::vector<MaterialRecord> &materials,
                          std::unordered_map<std::string, uint32_t> &materialIndices,
                          types::SceneData &sceneData) {
            std::ifstream file{path};
            if (!file)
                throw std::runtime_error("failed to open OBJ material library '" + path.string() + "'");

            MaterialRecord *current = nullptr;
            std::string line;
            size_t lineNumber = 0;
            while (std::getline(file, line)) {
                ++lineNumber;
                const std::string content = lineContent(std::move(line));
                if (content.empty())
                    continue;
                const size_t separator = content.find_first_of(" \t");
                const std::string directive = content.substr(0, separator);
                const std::string arguments = separator == std::string::npos
                    ? std::string{}
                    : trim(content.substr(separator + 1));

                if (directive == "newmtl") {
                    const std::string name = trim(arguments);
                    if (name.empty())
                        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                                 ": MTL newmtl is missing its name");
                    if (materials.size() >= std::numeric_limits<uint32_t>::max())
                        throw std::length_error("OBJ contains too many materials");
                    const uint32_t index = static_cast<uint32_t>(materials.size());
                    auto [found, inserted] = materialIndices.emplace(name, index);
                    if (!inserted)
                        throw std::runtime_error("duplicate MTL material name '" + name + "'");
                    materials.push_back({
                        .data = {.name = name},
                    });
                    current = &materials.back();
                } else if (current != nullptr && directive == "Kd") {
                    std::istringstream values{arguments};
                    float red{}, green{}, blue{};
                    if (!(values >> red >> green >> blue) ||
                        !std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue))
                        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                                 ": MTL Kd requires three finite color components");
                    current->data.baseColorFactor.r = red;
                    current->data.baseColorFactor.g = green;
                    current->data.baseColorFactor.b = blue;
                } else if (current != nullptr && directive == "d") {
                    const std::vector<std::string> words = splitWords(arguments);
                    if (words.size() != 1)
                        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                                 ": MTL d requires one opacity value");
                    current->data.baseColorFactor.a = parseFloat(words[0], path, lineNumber);
                } else if (current != nullptr && directive == "Tr") {
                    const std::vector<std::string> words = splitWords(arguments);
                    if (words.size() != 1)
                        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                                 ": MTL Tr requires one transparency value");
                    current->data.baseColorFactor.a = 1.0f - parseFloat(words[0], path, lineNumber);
                } else if (current != nullptr && directive == "map_Kd") {
                    if (current->textureIndex)
                        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                                 ": MTL material has more than one map_Kd");
                    const MtlTexturePath texturePath = readMtlTexturePath(arguments, path);
                    const std::filesystem::path &imagePath = texturePath.path;
                    if (sceneData.images.size() >= std::numeric_limits<uint32_t>::max() ||
                        sceneData.textures.size() >= std::numeric_limits<uint32_t>::max())
                        throw std::length_error("OBJ contains too many diffuse textures");
                    const std::vector<uint8_t> encoded = readFileBytes(imagePath);
                    const uint32_t imageIndex = static_cast<uint32_t>(sceneData.images.size());
                    sceneData.images.push_back(decodeImage(
                        imagePath.filename().string(), mimeTypeForPath(imagePath), encoded));
                    const uint32_t textureIndex = static_cast<uint32_t>(sceneData.textures.size());
                    std::optional<uint32_t> samplerIndex;
                    if (texturePath.clampToEdge) {
                        if (sceneData.samplers.size() >= std::numeric_limits<uint32_t>::max())
                            throw std::length_error("OBJ contains too many texture samplers");
                        samplerIndex = static_cast<uint32_t>(sceneData.samplers.size());
                        sceneData.samplers.push_back({
                            .wrapS = types::TextureWrap::ClampToEdge,
                            .wrapT = types::TextureWrap::ClampToEdge,
                        });
                    }
                    sceneData.textures.push_back({
                        .imageIndex = imageIndex,
                        .samplerIndex = samplerIndex,
                    });
                    current->textureIndex = textureIndex;
                    current->data.baseColorTextureIndex = textureIndex;
                }
            }
            if (file.bad())
                throw std::runtime_error("failed while reading OBJ material library '" + path.string() + "'");
        }

        struct MeshBuilder {
            types::MeshData mesh;
            std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexIndices;
            std::optional<uint32_t> activeMaterial;
            uint32_t activePrimitiveFirstIndex{};
            bool hasActivePrimitive{};
        };

        void finishPrimitive(MeshBuilder &builder) {
            if (!builder.hasActivePrimitive)
                return;
            const size_t end = builder.mesh.indices.size();
            const size_t count = end - builder.activePrimitiveFirstIndex;
            if (count != 0) {
                if (count > std::numeric_limits<uint32_t>::max())
                    throw std::length_error("OBJ material primitive has too many indices");
                builder.mesh.primitives.push_back({
                    .firstIndex = builder.activePrimitiveFirstIndex,
                    .indexCount = static_cast<uint32_t>(count),
                    .materialIndex = builder.activeMaterial,
                });
            }
            builder.hasActivePrimitive = false;
        }

        void selectMaterial(MeshBuilder &builder, const std::optional<uint32_t> materialIndex) {
            if (builder.hasActivePrimitive && builder.activeMaterial != materialIndex)
                finishPrimitive(builder);
            if (!builder.hasActivePrimitive) {
                if (builder.mesh.indices.size() > std::numeric_limits<uint32_t>::max())
                    throw std::length_error("OBJ mesh has too many indices");
                builder.activePrimitiveFirstIndex = static_cast<uint32_t>(builder.mesh.indices.size());
                builder.activeMaterial = materialIndex;
                builder.hasActivePrimitive = true;
            }
        }

        [[nodiscard]] glm::vec3 faceNormal(const glm::vec3 &first,
                                           const glm::vec3 &second,
                                           const glm::vec3 &third) {
            const glm::vec3 cross = glm::cross(second - first, third - first);
            const float lengthSquared = glm::dot(cross, cross);
            return lengthSquared > 1.0e-20f
                ? cross / std::sqrt(lengthSquared)
                : glm::vec3{0.0f, 0.0f, 1.0f};
        }
    }

    types::SceneData ObjImporter::load(const std::filesystem::path &path,
                                       const float aspectRatio) const {
        if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0f)
            throw std::invalid_argument("OBJ import aspect ratio must be finite and positive");

        std::ifstream file{path};
        if (!file)
            throw std::runtime_error("failed to open OBJ file '" + path.string() + "'");

        types::SceneData sceneData;
        sceneData.name = path.stem().string();
        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> uvs;
        std::vector<glm::vec3> normals;
        std::vector<MaterialRecord> materials;
        std::unordered_map<std::string, uint32_t> materialIndices;
        std::vector<std::filesystem::path> materialLibraries;
        std::string line;
        size_t lineNumber = 0;

        // Read libraries before geometry so usemtl may appear before or after
        // an mtllib statement, both of which are encountered in real files.
        std::vector<std::string> objectLines;
        while (std::getline(file, line)) {
            ++lineNumber;
            const std::string content = lineContent(std::move(line));
            if (content.empty())
                continue;
            const size_t separator = content.find_first_of(" \t");
            const std::string directive = content.substr(0, separator);
            const std::string arguments = separator == std::string::npos
                ? std::string{}
                : trim(content.substr(separator + 1));
            if (directive == "mtllib") {
                for (const std::string &library : splitWords(arguments)) {
                    const std::filesystem::path libraryPath{library};
                    materialLibraries.push_back(libraryPath.is_absolute()
                        ? libraryPath
                        : path.parent_path() / libraryPath);
                }
            }
            objectLines.push_back(content);
        }
        if (file.bad())
            throw std::runtime_error("failed while reading OBJ file '" + path.string() + "'");
        for (const auto &library : materialLibraries)
            parseMtlFile(library, materials, materialIndices, sceneData);

        MeshBuilder builder;
        std::string objectName = path.stem().string();
        std::optional<uint32_t> currentMaterial;
        uint32_t generatedNormalFace = 0;

        const auto flushObject = [&]() {
            finishPrimitive(builder);
            if (builder.mesh.primitives.empty()) {
                builder = MeshBuilder{};
                return;
            }
            if (sceneData.meshes.size() >= std::numeric_limits<uint32_t>::max())
                throw std::length_error("OBJ contains too many mesh objects");
            const uint32_t meshIndex = static_cast<uint32_t>(sceneData.meshes.size());
            sceneData.meshes.push_back(std::move(builder.mesh));
            sceneData.objects.push_back({
                .meshIndex = meshIndex,
                .worldTransform = glm::mat4{1.0f},
                .name = objectName,
            });
            builder = MeshBuilder{};
        };

        lineNumber = 0;
        for (const std::string &sourceLine : objectLines) {
            ++lineNumber;
            const size_t separator = sourceLine.find_first_of(" \t");
            const std::string directive = sourceLine.substr(0, separator);
            const std::string arguments = separator == std::string::npos
                ? std::string{}
                : trim(sourceLine.substr(separator + 1));

            if (directive == "v") {
                const auto words = splitWords(arguments);
                if (words.size() < 3)
                    throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                             ": OBJ vertex requires at least three coordinates");
                positions.emplace_back(
                    parseFloat(words[0], path, lineNumber),
                    parseFloat(words[1], path, lineNumber),
                    parseFloat(words[2], path, lineNumber));
            } else if (directive == "vt") {
                const auto words = splitWords(arguments);
                if (words.size() < 2)
                    throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                             ": OBJ texture coordinate requires at least two components");
                uvs.emplace_back(
                    parseFloat(words[0], path, lineNumber),
                    1.0f - parseFloat(words[1], path, lineNumber));
            } else if (directive == "vn") {
                const auto words = splitWords(arguments);
                if (words.size() < 3)
                    throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                             ": OBJ normal requires three components");
                normals.emplace_back(
                    parseFloat(words[0], path, lineNumber),
                    parseFloat(words[1], path, lineNumber),
                    parseFloat(words[2], path, lineNumber));
            } else if (directive == "mtllib") {
                continue;
            } else if (directive == "usemtl") {
                const auto found = materialIndices.find(trim(arguments));
                if (found == materialIndices.end())
                    throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                             ": OBJ usemtl refers to an undefined material '" + trim(arguments) + "'");
                currentMaterial = found->second;
                selectMaterial(builder, currentMaterial);
            } else if (directive == "o" || directive == "g") {
                flushObject();
                objectName = arguments.empty() ? path.stem().string() : arguments;
                if (builder.hasActivePrimitive)
                    selectMaterial(builder, currentMaterial);
            } else if (directive == "f") {
                const auto words = splitWords(arguments);
                if (words.size() < 3)
                    throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                             ": OBJ face requires at least three vertices");
                std::vector<FaceVertex> face;
                face.reserve(words.size());
                for (const auto &word : words)
                    face.push_back(parseFaceVertex(
                        word, positions.size(), uvs.size(), normals.size(), path, lineNumber));
                selectMaterial(builder, currentMaterial);

                for (size_t triangle = 1; triangle + 1 < face.size(); ++triangle) {
                    const FaceVertex triangleVertices[]{face[0], face[triangle], face[triangle + 1]};
                    const glm::vec3 generatedNormal = faceNormal(
                        positions[triangleVertices[0].positionIndex],
                        positions[triangleVertices[1].positionIndex],
                        positions[triangleVertices[2].positionIndex]);
                    if (generatedNormalFace == std::numeric_limits<uint32_t>::max())
                        throw std::length_error("OBJ contains too many faces for generated normals");
                    const uint32_t thisGeneratedNormalFace = generatedNormalFace++;

                    for (const FaceVertex &faceVertex : triangleVertices) {
                        const VertexKey key{
                            .positionIndex = faceVertex.positionIndex,
                            .uvIndex = faceVertex.uvIndex,
                            .normalIndex = faceVertex.normalIndex,
                            .generatedNormalFace = faceVertex.normalIndex == kMissingIndex
                                ? thisGeneratedNormalFace
                                : 0,
                        };
                        const auto found = builder.vertexIndices.find(key);
                        uint32_t vertexIndex{};
                        if (found != builder.vertexIndices.end()) {
                            vertexIndex = found->second;
                        } else {
                            if (builder.mesh.vertices.size() >= std::numeric_limits<uint32_t>::max())
                                throw std::length_error("OBJ mesh contains too many vertices");
                            vertexIndex = static_cast<uint32_t>(builder.mesh.vertices.size());
                            const glm::vec3 sourceNormal = faceVertex.normalIndex == kMissingIndex
                                ? generatedNormal
                                : normals[faceVertex.normalIndex];
                            const float normalLengthSquared = glm::dot(sourceNormal, sourceNormal);
                            const glm::vec3 normal = normalLengthSquared > 1.0e-20f
                                ? sourceNormal / std::sqrt(normalLengthSquared)
                                : generatedNormal;
                            const glm::vec2 uv = faceVertex.uvIndex == kMissingIndex
                                ? glm::vec2{0.0f}
                                : uvs[faceVertex.uvIndex];
                            builder.mesh.vertices.emplace_back(
                                positions[faceVertex.positionIndex], normal, uv,
                                glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
                            builder.vertexIndices.emplace(key, vertexIndex);
                        }
                        builder.mesh.indices.push_back(vertexIndex);
                    }
                }
            }
            // Other Wavefront directives (smoothing groups, vertex colors,
            // and non-diffuse MTL properties) do not affect this static slice.
        }
        flushObject();

        if (sceneData.meshes.empty())
            throw std::runtime_error("OBJ file contains no triangle faces: '" + path.string() + "'");

        sceneData.materials.reserve(materials.size());
        for (auto &material : materials)
            sceneData.materials.push_back(std::move(material.data));

        addDefaultCamera(sceneData, aspectRatio);
        return sceneData;
    }
}
