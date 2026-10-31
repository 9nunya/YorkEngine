#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "assets/default_camera.hxx"
#include "assets/image_decode.hxx"
#include "assets/gltf_importer.hxx"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace york::assets {
    namespace {
        struct DataDeleter {
            void operator()(cgltf_data *data) const {
                cgltf_free(data);
            }
        };

        using DataPtr = std::unique_ptr<cgltf_data, DataDeleter>;

        [[nodiscard]] std::string resultMessage(const cgltf_result result) {
            return "cgltf error " + std::to_string(static_cast<int>(result));
        }

        [[nodiscard]] std::string stringOr(const char *value, std::string fallback) {
            return value != nullptr && value[0] != '\0' ? value : std::move(fallback);
        }

        [[nodiscard]] int base64Value(const char character) {
            if (character >= 'A' && character <= 'Z') return character - 'A';
            if (character >= 'a' && character <= 'z') return character - 'a' + 26;
            if (character >= '0' && character <= '9') return character - '0' + 52;
            if (character == '+') return 62;
            if (character == '/') return 63;
            return -1;
        }

        [[nodiscard]] std::vector<uint8_t> decodeBase64(const std::string_view encoded) {
            if (encoded.empty() || encoded.size() % 4 != 0)
                throw std::runtime_error("glTF base64 data URI has invalid padding");
            const size_t padding = encoded.ends_with("==") ? 2 : encoded.ends_with("=") ? 1 : 0;
            std::vector<uint8_t> bytes;
            bytes.reserve(encoded.size() / 4 * 3 - padding);

            for (size_t offset = 0; offset < encoded.size(); offset += 4) {
                const bool finalBlock = offset + 4 == encoded.size();
                const int a = base64Value(encoded[offset]);
                const int b = base64Value(encoded[offset + 1]);
                const int c = encoded[offset + 2] == '=' ? 0 : base64Value(encoded[offset + 2]);
                const int d = encoded[offset + 3] == '=' ? 0 : base64Value(encoded[offset + 3]);
                if (a < 0 || b < 0 || c < 0 || d < 0 ||
                    (!finalBlock && (encoded[offset + 2] == '=' || encoded[offset + 3] == '=')) ||
                    (encoded[offset + 2] == '=' && encoded[offset + 3] != '='))
                    throw std::runtime_error("glTF base64 data URI contains invalid characters");

                const uint32_t block = (static_cast<uint32_t>(a) << 18) |
                                       (static_cast<uint32_t>(b) << 12) |
                                       (static_cast<uint32_t>(c) << 6) |
                                       static_cast<uint32_t>(d);
                bytes.push_back(static_cast<uint8_t>((block >> 16) & 0xff));
                if (encoded[offset + 2] != '=')
                    bytes.push_back(static_cast<uint8_t>((block >> 8) & 0xff));
                if (encoded[offset + 3] != '=')
                    bytes.push_back(static_cast<uint8_t>(block & 0xff));
            }
            if (bytes.size() != encoded.size() / 4 * 3 - padding)
                throw std::runtime_error("glTF base64 data URI has inconsistent padding");
            return bytes;
        }

        [[nodiscard]] uint8_t hexValue(const char character) {
            if (character >= '0' && character <= '9') return static_cast<uint8_t>(character - '0');
            if (character >= 'a' && character <= 'f') return static_cast<uint8_t>(character - 'a' + 10);
            if (character >= 'A' && character <= 'F') return static_cast<uint8_t>(character - 'A' + 10);
            throw std::runtime_error("glTF URI contains an invalid percent escape");
        }

        [[nodiscard]] std::vector<uint8_t> decodeUriPayload(const std::string_view payload) {
            std::vector<uint8_t> bytes;
            bytes.reserve(payload.size());
            for (size_t index = 0; index < payload.size(); ++index) {
                if (payload[index] == '%') {
                    if (index + 2 >= payload.size())
                        throw std::runtime_error("glTF URI contains an incomplete percent escape");
                    bytes.push_back(static_cast<uint8_t>((hexValue(payload[index + 1]) << 4) |
                                                         hexValue(payload[index + 2])));
                    index += 2;
                } else {
                    bytes.push_back(static_cast<uint8_t>(payload[index]));
                }
            }
            return bytes;
        }

        [[nodiscard]] std::vector<uint8_t> readExternalImage(
            const std::filesystem::path &assetPath,
            const std::string_view uri) {
            if (uri.find("://") != std::string_view::npos)
                throw std::runtime_error("remote glTF image URIs are not supported");
            const auto decodedPath = decodeUriPayload(uri);
            // URIs are raw byte strings; constructing the path from the byte
            // range keeps the encoding platform-default, the same choice
            // cgltf makes for buffer paths inside cgltf_load_buffers.
            const std::filesystem::path relativePath(
                reinterpret_cast<const char *>(decodedPath.data()),
                reinterpret_cast<const char *>(decodedPath.data()) + decodedPath.size());
            const std::filesystem::path imagePath = relativePath.is_absolute()
                ? relativePath
                : assetPath.parent_path() / relativePath;

            std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error("failed to open glTF image '" + imagePath.string() + "'");
            const std::streampos end = file.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max())
                throw std::runtime_error("glTF image file is empty or too large");
            std::vector<uint8_t> bytes(static_cast<size_t>(end));
            file.seekg(0, std::ios::beg);
            if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("failed to read glTF image '" + imagePath.string() + "'");
            return bytes;
        }

        [[nodiscard]] std::vector<uint8_t> readImageBytes(
            const cgltf_image &image,
            const std::filesystem::path &assetPath,
            std::string &mimeType) {
            if (image.buffer_view != nullptr) {
                const uint8_t *begin = cgltf_buffer_view_data(image.buffer_view);
                if (begin == nullptr || image.buffer_view->size == 0)
                    throw std::runtime_error("glTF embedded image buffer view is empty");
                mimeType = stringOr(image.mime_type, "application/octet-stream");
                return {begin, begin + image.buffer_view->size};
            }

            if (image.uri == nullptr)
                throw std::runtime_error("glTF image has neither a URI nor a buffer view");
            const std::string_view uri{image.uri};
            if (!uri.starts_with("data:")) {
                const auto decodedUri = decodeUriPayload(uri);
                const std::filesystem::path decodedPath(
                    reinterpret_cast<const char *>(decodedUri.data()),
                    reinterpret_cast<const char *>(decodedUri.data()) + decodedUri.size());
                const std::string extension = decodedPath.extension().string();
                if (image.mime_type != nullptr)
                    mimeType = image.mime_type;
                else if (extension == ".png" || extension == ".PNG")
                    mimeType = "image/png";
                else if (extension == ".jpg" || extension == ".jpeg" ||
                         extension == ".JPG" || extension == ".JPEG")
                    mimeType = "image/jpeg";
                else
                    mimeType = "application/octet-stream";
                return readExternalImage(assetPath, uri);
            }

            const size_t comma = uri.find(',');
            if (comma == std::string_view::npos)
                throw std::runtime_error("glTF image data URI is missing its payload separator");
            const std::string_view header = uri.substr(5, comma - 5);
            const size_t parameterStart = header.find(';');
            mimeType = parameterStart == std::string_view::npos
                ? std::string(header)
                : std::string(header.substr(0, parameterStart));
            const std::string_view payload = uri.substr(comma + 1);
            if (header.ends_with(";base64"))
                return decodeBase64(payload);
            return decodeUriPayload(payload);
        }

        [[nodiscard]] types::ImageData readImage(
            const cgltf_image &sourceImage,
            const size_t imageIndex,
            const std::filesystem::path &assetPath) {
            std::string mimeType;
            const auto encodedBytes = readImageBytes(sourceImage, assetPath, mimeType);
            return decodeImage(
                stringOr(sourceImage.name, "image_" + std::to_string(imageIndex)),
                std::move(mimeType),
                encodedBytes);
        }

        [[nodiscard]] types::TextureFilter readFilter(const cgltf_filter_type filter,
                                                       const types::TextureFilter defaultFilter) {
            switch (filter) {
                case cgltf_filter_type_nearest: return types::TextureFilter::Nearest;
                case cgltf_filter_type_linear: return types::TextureFilter::Linear;
                case cgltf_filter_type_nearest_mipmap_nearest: return types::TextureFilter::NearestMipmapNearest;
                case cgltf_filter_type_linear_mipmap_nearest: return types::TextureFilter::LinearMipmapNearest;
                case cgltf_filter_type_nearest_mipmap_linear: return types::TextureFilter::NearestMipmapLinear;
                case cgltf_filter_type_linear_mipmap_linear: return types::TextureFilter::LinearMipmapLinear;
                case cgltf_filter_type_undefined: return defaultFilter;
            }
            throw std::runtime_error("glTF sampler uses an unsupported filter value");
        }

        [[nodiscard]] types::TextureWrap readWrap(const cgltf_wrap_mode wrap) {
            switch (wrap) {
                case cgltf_wrap_mode_clamp_to_edge: return types::TextureWrap::ClampToEdge;
                case cgltf_wrap_mode_mirrored_repeat: return types::TextureWrap::MirroredRepeat;
                case cgltf_wrap_mode_repeat: return types::TextureWrap::Repeat;
            }
            throw std::runtime_error("glTF sampler uses an unsupported wrap value");
        }

        [[nodiscard]] types::SamplerData readSampler(const cgltf_sampler &sourceSampler) {
            return {
                .magFilter = readFilter(sourceSampler.mag_filter, types::TextureFilter::Linear),
                .minFilter = readFilter(sourceSampler.min_filter, types::TextureFilter::LinearMipmapLinear),
                .wrapS = readWrap(sourceSampler.wrap_s),
                .wrapT = readWrap(sourceSampler.wrap_t),
            };
        }

        void rejectMeshoptCompression(const cgltf_accessor &accessor) {
            if (accessor.buffer_view != nullptr && accessor.buffer_view->has_meshopt_compression)
                throw std::runtime_error("EXT_meshopt_compression is not supported by the glTF importer");
            if (accessor.is_sparse &&
                (accessor.sparse.indices_buffer_view->has_meshopt_compression ||
                 accessor.sparse.values_buffer_view->has_meshopt_compression))
                throw std::runtime_error("sparse EXT_meshopt_compression accessors are not supported");
        }

        void validateAttributeFormat(const cgltf_accessor &accessor,
                                     const cgltf_type expectedType,
                                     const cgltf_attribute_type semantic) {
            if (accessor.type != expectedType)
                throw std::runtime_error("glTF vertex attribute has an unexpected accessor type");
            rejectMeshoptCompression(accessor);

            if (semantic == cgltf_attribute_type_position) {
                if (accessor.component_type != cgltf_component_type_r_32f || accessor.normalized)
                    throw std::runtime_error("glTF POSITION must use 32-bit floating-point components");
                return;
            }

            if (accessor.component_type == cgltf_component_type_r_32f)
                return;

            const bool signedNormalized = accessor.normalized &&
                (accessor.component_type == cgltf_component_type_r_8 ||
                 accessor.component_type == cgltf_component_type_r_16);
            const bool unsignedNormalizedTexcoord = semantic == cgltf_attribute_type_texcoord &&
                accessor.normalized &&
                (accessor.component_type == cgltf_component_type_r_8u ||
                 accessor.component_type == cgltf_component_type_r_16u);
            if (!signedNormalized && !unsignedNormalizedTexcoord)
                throw std::runtime_error("glTF vertex attribute component format is unsupported");
        }

        [[nodiscard]] std::vector<float> unpackAttribute(
            const cgltf_accessor &accessor,
            const cgltf_type expectedType,
            const cgltf_attribute_type semantic) {
            validateAttributeFormat(accessor, expectedType, semantic);
            const cgltf_size componentCount = cgltf_num_components(expectedType);
            if (accessor.count > std::numeric_limits<cgltf_size>::max() / componentCount)
                throw std::length_error("glTF vertex attribute is too large");

            std::vector<float> values(accessor.count * componentCount);
            const cgltf_size unpackedCount = cgltf_accessor_unpack_floats(
                &accessor, values.data(), values.size());
            if (unpackedCount != values.size())
                throw std::runtime_error("failed to decode glTF vertex attribute accessor");
            for (const float value : values)
                if (!std::isfinite(value))
                    throw std::runtime_error("glTF vertex attribute contains a non-finite value");
            return values;
        }

        [[nodiscard]] glm::vec3 readVec3(const std::vector<float> &values,
                                         const size_t index) {
            const size_t offset = index * 3;
            return {values[offset], values[offset + 1], values[offset + 2]};
        }

        [[nodiscard]] glm::vec2 readVec2(const std::vector<float> &values,
                                         const size_t index) {
            const size_t offset = index * 2;
            return {values[offset], values[offset + 1]};
        }

        [[nodiscard]] glm::vec4 readVec4(const std::vector<float> &values,
                                         const size_t index) {
            const size_t offset = index * 4;
            return {values[offset], values[offset + 1], values[offset + 2], values[offset + 3]};
        }

        [[nodiscard]] const cgltf_accessor *findAttribute(
            const cgltf_primitive &primitive,
            const cgltf_attribute_type semantic,
            const cgltf_int index = 0) {
            return cgltf_find_accessor(&primitive, semantic, index);
        }

        [[nodiscard]] types::MeshData readMesh(const cgltf_data &document,
                                               const cgltf_mesh &sourceMesh) {
            if (sourceMesh.primitives_count == 0)
                throw std::runtime_error("glTF mesh contains no primitives");

            types::MeshData meshData;
            for (cgltf_size primitiveIndex = 0;
                 primitiveIndex < sourceMesh.primitives_count;
                 ++primitiveIndex) {
                const cgltf_primitive &primitive = sourceMesh.primitives[primitiveIndex];
                if (primitive.type != cgltf_primitive_type_triangles)
                    throw std::runtime_error("only glTF triangle-list primitives are supported");
                if (primitive.has_draco_mesh_compression)
                    throw std::runtime_error("KHR_draco_mesh_compression is not supported");
                if (primitive.targets_count != 0)
                    throw std::runtime_error("glTF morph targets are not supported");

                const cgltf_accessor *positionAccessor =
                    findAttribute(primitive, cgltf_attribute_type_position);
                if (positionAccessor == nullptr)
                    throw std::runtime_error("glTF triangle primitive is missing POSITION");
                if (positionAccessor->count == 0)
                    throw std::runtime_error("glTF POSITION accessor is empty");
                const size_t vertexCount = positionAccessor->count;
                const size_t maxElementCount = std::numeric_limits<uint32_t>::max();
                if (vertexCount > maxElementCount ||
                    meshData.vertices.size() > maxElementCount - vertexCount)
                    throw std::length_error("glTF mesh has too many vertices for the renderer");

                const cgltf_accessor *normalAccessor =
                    findAttribute(primitive, cgltf_attribute_type_normal);
                const cgltf_accessor *uvAccessor =
                    findAttribute(primitive, cgltf_attribute_type_texcoord);
                const cgltf_accessor *tangentAccessor =
                    findAttribute(primitive, cgltf_attribute_type_tangent);
                if ((normalAccessor != nullptr && normalAccessor->count != vertexCount) ||
                    (uvAccessor != nullptr && uvAccessor->count != vertexCount) ||
                    (tangentAccessor != nullptr && tangentAccessor->count != vertexCount))
                    throw std::runtime_error("glTF primitive attributes have different vertex counts");

                const auto positions = unpackAttribute(
                    *positionAccessor, cgltf_type_vec3, cgltf_attribute_type_position);
                const auto normals = normalAccessor == nullptr
                    ? std::vector<float>{}
                    : unpackAttribute(*normalAccessor, cgltf_type_vec3, cgltf_attribute_type_normal);
                const auto uvs = uvAccessor == nullptr
                    ? std::vector<float>{}
                    : unpackAttribute(*uvAccessor, cgltf_type_vec2, cgltf_attribute_type_texcoord);
                const auto tangents = tangentAccessor == nullptr
                    ? std::vector<float>{}
                    : unpackAttribute(*tangentAccessor, cgltf_type_vec4, cgltf_attribute_type_tangent);

                const uint32_t firstVertex = static_cast<uint32_t>(meshData.vertices.size());
                for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                    // Missing optional attributes get deterministic neutral values for the current vertex format.
                    const glm::vec3 normal = normalAccessor == nullptr
                        ? glm::vec3{0.0f, 0.0f, 1.0f}
                        : readVec3(normals, vertexIndex);
                    const glm::vec2 uv = uvAccessor == nullptr
                        ? glm::vec2{0.0f}
                        : readVec2(uvs, vertexIndex);
                    const glm::vec4 tangent = tangentAccessor == nullptr
                        ? glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}
                        : readVec4(tangents, vertexIndex);
                    meshData.vertices.emplace_back(
                        readVec3(positions, vertexIndex), normal, uv, tangent);
                }

                // The vertex format stores one uv pair for TEXCOORD_0; any other
                // set would silently sample wrong coordinates, so reject it.
                for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                    const cgltf_attribute &attribute_ = primitive.attributes[attribute];
                    if (attribute_.type == cgltf_attribute_type_texcoord && attribute_.index != 0)
                        throw std::runtime_error("glTF TEXCOORD_" + std::to_string(attribute_.index) +
                                                 " is unsupported; only TEXCOORD_0 is imported");
                }

                const size_t incomingIndexCount = primitive.indices != nullptr
                    ? primitive.indices->count
                    : vertexCount;
                if (incomingIndexCount > maxElementCount ||
                    meshData.indices.size() > maxElementCount - incomingIndexCount)
                    throw std::length_error("glTF mesh has too many indices for the renderer");
                const uint32_t firstIndex = static_cast<uint32_t>(meshData.indices.size());
                if (primitive.indices != nullptr) {
                    const cgltf_accessor &indexAccessor = *primitive.indices;
                    rejectMeshoptCompression(indexAccessor);
                    if (indexAccessor.type != cgltf_type_scalar || indexAccessor.is_sparse)
                        throw std::runtime_error("glTF indices must be non-sparse scalar accessors");
                    if (indexAccessor.normalized)
                        throw std::runtime_error("glTF index accessors cannot be normalized");
                    if (indexAccessor.component_type != cgltf_component_type_r_8u &&
                        indexAccessor.component_type != cgltf_component_type_r_16u &&
                        indexAccessor.component_type != cgltf_component_type_r_32u)
                        throw std::runtime_error("glTF indices must use unsigned 8-, 16-, or 32-bit values");
                    if (indexAccessor.count > std::numeric_limits<uint32_t>::max())
                        throw std::length_error("glTF primitive has too many indices");

                    for (cgltf_size index = 0; index < indexAccessor.count; ++index) {
                        const cgltf_size localIndex = cgltf_accessor_read_index(&indexAccessor, index);
                        if (localIndex >= vertexCount)
                            throw std::runtime_error("glTF index is outside the primitive vertex array");
                        meshData.indices.push_back(firstVertex + static_cast<uint32_t>(localIndex));
                    }
                } else {
                    if (vertexCount > std::numeric_limits<uint32_t>::max())
                        throw std::length_error("unindexed glTF primitive has too many vertices");
                    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
                        meshData.indices.push_back(firstVertex + static_cast<uint32_t>(vertexIndex));
                }

                const size_t primitiveIndexCount = meshData.indices.size() - firstIndex;
                if (primitiveIndexCount == 0 || primitiveIndexCount % 3 != 0)
                    throw std::runtime_error("glTF triangle primitive index count must be a non-zero multiple of three");
                if (primitiveIndexCount > std::numeric_limits<uint32_t>::max())
                    throw std::length_error("glTF primitive has too many indices");

                std::optional<uint32_t> materialIndex;
                if (primitive.material != nullptr) {
                    const cgltf_size sourceMaterialIndex = cgltf_material_index(&document, primitive.material);
                    if (sourceMaterialIndex > std::numeric_limits<uint32_t>::max())
                        throw std::length_error("glTF material index is too large");
                    materialIndex = static_cast<uint32_t>(sourceMaterialIndex);
                    // A textured material needs UVs to sample with; without them
                    // the fragment shader would sample the zero UV on every vertex.
                    const cgltf_material &documentMaterial = document.materials[sourceMaterialIndex];
                    if (documentMaterial.pbr_metallic_roughness.base_color_texture.texture != nullptr &&
                        uvAccessor == nullptr)
                        throw std::runtime_error("glTF primitive has a textured base color but no TEXCOORD_0");
                }
                meshData.primitives.push_back({
                    .firstIndex = firstIndex,
                    .indexCount = static_cast<uint32_t>(primitiveIndexCount),
                    .materialIndex = materialIndex,
                });
            }
            return meshData;
        }

        [[nodiscard]] glm::mat4 nodeWorldTransform(const cgltf_node &node) {
            std::array<cgltf_float, 16> matrix{};
            cgltf_node_transform_world(&node, matrix.data());
            return glm::make_mat4(matrix.data());
        }

        [[nodiscard]] types::SceneCameraData readCamera(
            const cgltf_node &node,
            const float defaultAspectRatio) {
            const cgltf_camera &sourceCamera = *node.camera;
            types::SceneCameraData cameraData;
            cameraData.view = glm::inverse(nodeWorldTransform(node));
            cameraData.name = stringOr(sourceCamera.name, "glTF camera");

            if (sourceCamera.type == cgltf_camera_type_perspective) {
                const auto &perspective = sourceCamera.data.perspective;
                const float aspectRatio = perspective.has_aspect_ratio
                    ? perspective.aspect_ratio
                    : defaultAspectRatio;
                const float zFar = perspective.has_zfar ? perspective.zfar : 10000.0f;
                if (!std::isfinite(perspective.yfov) || !std::isfinite(aspectRatio) ||
                    !std::isfinite(perspective.znear) || !std::isfinite(zFar) ||
                    perspective.yfov <= 0.0f || aspectRatio <= 0.0f ||
                    perspective.znear <= 0.0f || zFar <= perspective.znear)
                    throw std::runtime_error("glTF perspective camera has invalid projection parameters");
                cameraData.projection = glm::perspectiveRH_ZO(
                    perspective.yfov, aspectRatio, perspective.znear, zFar);
            } else if (sourceCamera.type == cgltf_camera_type_orthographic) {
                const auto &orthographic = sourceCamera.data.orthographic;
                if (!std::isfinite(orthographic.xmag) || !std::isfinite(orthographic.ymag) ||
                    !std::isfinite(orthographic.znear) || !std::isfinite(orthographic.zfar) ||
                    orthographic.xmag <= 0.0f || orthographic.ymag <= 0.0f ||
                    orthographic.znear < 0.0f || orthographic.zfar <= orthographic.znear)
                    throw std::runtime_error("glTF orthographic camera has invalid projection parameters");
                cameraData.projection = glm::orthoRH_ZO(
                    -orthographic.xmag, orthographic.xmag,
                    -orthographic.ymag, orthographic.ymag,
                    orthographic.znear, orthographic.zfar);
            } else {
                throw std::runtime_error("glTF camera type is unsupported");
            }
            return cameraData;
        }

    }

    types::SceneData GltfImporter::load(const std::filesystem::path &path,
                                        const float aspectRatio) const {
        if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0f)
            throw std::invalid_argument("glTF import aspect ratio must be finite and positive");

        const std::string nativePath = path.string();
        cgltf_options options{};
        cgltf_data *rawData = nullptr;
        const cgltf_result parseResult = cgltf_parse_file(&options, nativePath.c_str(), &rawData);
        if (parseResult != cgltf_result_success)
            throw std::runtime_error("failed to parse glTF '" + nativePath + "': " + resultMessage(parseResult));
        DataPtr document{rawData};

        const cgltf_result loadResult = cgltf_load_buffers(&options, document.get(), nativePath.c_str());
        if (loadResult != cgltf_result_success)
            throw std::runtime_error("failed to load glTF buffers for '" + nativePath + "': " + resultMessage(loadResult));
        const cgltf_result validationResult = cgltf_validate(document.get());
        if (validationResult != cgltf_result_success)
            throw std::runtime_error("invalid glTF '" + nativePath + "': " + resultMessage(validationResult));
        if (document->animations_count != 0)
            throw std::runtime_error("animated glTF scenes are not supported by the static scene importer");

        // Reject unsupported texture codecs before decoding any pixels so
        // basisu/webp scenes meet this error, not a decoder failure.
        for (cgltf_size texture = 0; texture < document->textures_count; ++texture)
            if (document->textures[texture].has_basisu || document->textures[texture].has_webp)
                throw std::runtime_error("basisu/webp image extensions are not supported by the glTF importer");

        const cgltf_scene *sourceScene = document->scene;
        if (sourceScene == nullptr && document->scenes_count != 0)
            sourceScene = &document->scenes[0];

        types::SceneData sceneData;
        sceneData.name = sourceScene != nullptr
            ? stringOr(sourceScene->name, path.stem().string())
            : path.stem().string();

        // Decode images up front so image/texture/sampler/material relations can be
        // resolved while the document is alive.
        sceneData.images.reserve(document->images_count);
        for (cgltf_size image = 0; image < document->images_count; ++image)
            sceneData.images.push_back(readImage(document->images[image], image, path));

        sceneData.samplers.reserve(document->samplers_count);
        for (cgltf_size sampler = 0; sampler < document->samplers_count; ++sampler)
            sceneData.samplers.push_back(readSampler(document->samplers[sampler]));

        sceneData.textures.reserve(document->textures_count);
        for (cgltf_size texture = 0; texture < document->textures_count; ++texture) {
            const cgltf_texture &sourceTexture = document->textures[texture];
            const cgltf_size imageIndex = cgltf_image_index(document.get(), sourceTexture.image);
            const std::optional<uint32_t> samplerIndex =
                sourceTexture.sampler != nullptr
                    ? std::optional<uint32_t>{
                        static_cast<uint32_t>(cgltf_sampler_index(document.get(), sourceTexture.sampler))}
                    : std::nullopt;
            sceneData.textures.push_back({
                .imageIndex = static_cast<uint32_t>(imageIndex),
                .samplerIndex = samplerIndex,
            });
        }

        sceneData.materials.reserve(document->materials_count);
        for (cgltf_size material = 0; material < document->materials_count; ++material) {
            const cgltf_material &sourceMaterial = document->materials[material];
            types::SceneMaterialData materialData{
                .name = stringOr(sourceMaterial.name,
                                 "material_" + std::to_string(material)),
                .baseColorFactor = glm::make_vec4(sourceMaterial.pbr_metallic_roughness.base_color_factor),
            };
            if (sourceMaterial.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
                const cgltf_texture_view &view = sourceMaterial.pbr_metallic_roughness.base_color_texture;
                if (view.texcoord != 0)
                    throw std::runtime_error("glTF base-color textures on TEXCOORD_" +
                        std::to_string(view.texcoord) + " are unsupported");
                if (view.has_transform)
                    throw std::runtime_error(
                        "glTF KHR_texture_transform is not supported by this slice");
                materialData.baseColorTextureIndex =
                    static_cast<uint32_t>(cgltf_texture_index(document.get(), view.texture));
            }
            materialData.alphaMode = sourceMaterial.alpha_mode == cgltf_alpha_mode_opaque
                ? types::MaterialAlphaMode::Opaque
                : sourceMaterial.alpha_mode == cgltf_alpha_mode_mask
                ? types::MaterialAlphaMode::Mask
                : types::MaterialAlphaMode::Blend;
            materialData.alphaCutoff = sourceMaterial.alpha_cutoff;
            // Unlit and alpha modes other than opaque are rendered by a later slice.
            if (sourceMaterial.unlit || sourceMaterial.alpha_mode != cgltf_alpha_mode_opaque)
                throw std::runtime_error("glTF material alpha/unlit modes are not enabled in this slice");
            sceneData.materials.push_back(std::move(materialData));
        }
        std::vector<std::optional<uint32_t>> meshIndices(document->meshes_count);

        const auto ensureMesh = [&](const cgltf_mesh &sourceMesh) -> uint32_t {
            const cgltf_size sourceIndex = cgltf_mesh_index(document.get(), &sourceMesh);
            if (sourceIndex >= meshIndices.size())
                throw std::runtime_error("glTF node refers to a mesh outside the document mesh array");
            if (!meshIndices[sourceIndex]) {
                if (sceneData.meshes.size() >= std::numeric_limits<uint32_t>::max())
                    throw std::length_error("glTF scene contains too many meshes");
                meshIndices[sourceIndex] = static_cast<uint32_t>(sceneData.meshes.size());
                sceneData.meshes.push_back(readMesh(*document, sourceMesh));
            }
            return *meshIndices[sourceIndex];
        };

        const auto visitNode = [&](const auto &self, const cgltf_node &node) -> void {
            if (node.has_mesh_gpu_instancing)
                throw std::runtime_error("EXT_mesh_gpu_instancing is not supported by the glTF importer");
            if (node.skin != nullptr)
                throw std::runtime_error("skinned glTF nodes are not supported by the static mesh importer");

            const glm::mat4 worldTransform = nodeWorldTransform(node);
            if (node.mesh != nullptr) {
                const uint32_t meshIndex = ensureMesh(*node.mesh);
                const cgltf_size nodeIndex = cgltf_node_index(document.get(), &node);
                const std::string nodeName = stringOr(
                    node.name, "node_" + std::to_string(nodeIndex));
                sceneData.objects.push_back({
                    .meshIndex = meshIndex,
                    .worldTransform = worldTransform,
                    .name = nodeName,
                });
            }

            if (node.camera != nullptr) {
                if (!sceneData.activeCameraIndex)
                    sceneData.activeCameraIndex = static_cast<uint32_t>(sceneData.cameras.size());
                sceneData.cameras.push_back(readCamera(node, aspectRatio));
            }

            for (cgltf_size child = 0; child < node.children_count; ++child)
                self(self, *node.children[child]);
        };

        if (sourceScene != nullptr) {
            for (cgltf_size root = 0; root < sourceScene->nodes_count; ++root)
                visitNode(visitNode, *sourceScene->nodes[root]);
        } else {
            for (cgltf_size node = 0; node < document->nodes_count; ++node)
                if (document->nodes[node].parent == nullptr)
                    visitNode(visitNode, document->nodes[node]);
        }

        if (!sceneData.activeCameraIndex)
            addDefaultCamera(sceneData, aspectRatio);
        return sceneData;
    }
}
