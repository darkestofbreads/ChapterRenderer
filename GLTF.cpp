#define STB_IMAGE_IMPLEMENTATION

#include "GLTF.h"


GLTF::GLTF(const std::filesystem::path& path) {
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (auto error = data.error(); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }

    auto p = fastgltf::Parser(fastgltf::Extensions::KHR_lights_punctual);
    auto gltf = p.loadGltf(data.get(), path.parent_path());
    if (auto error = gltf.error(); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
    asset = std::move(gltf.get());

    if (auto error = fastgltf::validate(asset); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
}

uint32_t ParseGLTFImage(const fastgltf::TextureInfo& imageInfo, const fastgltf::Asset& asset, std::vector<AllocatedImage>& txtrs, Uploader& uploader, vk::Device device, VmaAllocator allocator, vk::Sampler sampler) {
    const auto& texture          = asset.textures[imageInfo.textureIndex];
    const auto& image            = asset.images[texture.imageIndex.value()];
    const auto& sourceBufferView = get<fastgltf::sources::BufferView>(image.data);

    const auto& imageBufferView  = asset.bufferViews[sourceBufferView.bufferViewIndex];
    const auto& imageBuffer      = asset.buffers[imageBufferView.bufferIndex];
    const auto& imageData        = get<fastgltf::sources::Array>(imageBuffer.data);

    std::vector<unsigned char> imageChars(imageBufferView.byteLength);
    std::memcpy(imageChars.data(), imageData.bytes.data() + imageBufferView.byteOffset, imageBufferView.byteLength);
    if (sourceBufferView.mimeType == fastgltf::MimeType::JPEG || sourceBufferView.mimeType == fastgltf::MimeType::PNG) {
        int width, height, comp;
        const std::shared_ptr<void> pixels(
            stbi_load_from_memory(imageChars.data(), static_cast<int>(imageBufferView.byteLength), &width, &height, &comp, STBI_rgb_alpha),
            stbi_image_free
        );

        txtrs.emplace_back(CreateAllocatedImage(device, allocator, width, height, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled, sampler));
        uploader.StageUpload(txtrs[txtrs.size()-1], pixels, width*height*4);
    }
    else
        return 0;
    return txtrs.size() - 1;
}

void GLTF::LoadMaterials(std::vector<uint32_t> &materialIDs, std::vector<MaterialIndexGroup>& materialIndexGroups, std::vector<AllocatedImage>& textures, VmaAllocator allocator, vk::Device device, vk::Sampler sampler, Uploader& uploader)
{
    for (const auto& material : asset.materials) {
        MaterialIndexGroup matIndices{};
        const auto& pbrData = material.pbrData;

        if (pbrData.baseColorTexture.has_value())
            matIndices.diffuse = ParseGLTFImage(pbrData.baseColorTexture.value(), asset, textures, uploader, device, allocator, sampler);
        else
            matIndices.diffuse = 0;

        if (pbrData.metallicRoughnessTexture.has_value())
            matIndices.metallicRoughness = ParseGLTFImage(pbrData.metallicRoughnessTexture.value(), asset, textures, uploader, device, allocator, sampler);
        else
            matIndices.metallicRoughness = 1;

        if (material.emissiveTexture.has_value())
            matIndices.emissive = ParseGLTFImage(material.emissiveTexture.value(), asset, textures, uploader, device, allocator, sampler);
        else
            matIndices.emissive = 1;

        materialIDs.emplace_back(materialIndexGroups.size());
        materialIndexGroups.emplace_back(matIndices);
    }

}

//void GLTF::LoadLights()
//{
//    for (const auto& node : asset.nodes) {
//        if (node.lightIndex.has_value()) {
//            const auto& light = asset.lights[node.lightIndex.value()];
//            const auto& nodeData = get<fastgltf::TRS>(node.transform);
//            Light l{};
//            switch (light.type) {
//            case fastgltf::LightType::Point:
//                l.lightType = 0;
//                l.color   = glm::vec3(light.color.x(), light.color.y(), light.color.z());
//                l.pos     = glm::vec3(transform * glm::vec4(nodeData.translation.x(), nodeData.translation.y(), nodeData.translation.z(), 1));
//                l.falloff = 0;
//                l.radius  = 100;
//                if (light.range.has_value())
//                    l.radius = light.range.value();
//
//                pointLights.emplace_back(l);
//                break;
//            case fastgltf::LightType::Spot:
//                l.color       = glm::vec3(light.color.x(), light.color.y(), light.color.z());
//                l.pos         = glm::vec3(transform * glm::vec4(nodeData.translation.x(), nodeData.translation.y(), nodeData.translation.z(), 1));
//                //sl.lightDir = glm::fquat(nodeData.rotation.w(), nodeData.rotation.x(), nodeData.rotation.y(), nodeData.rotation.z());
//                l.falloff     = 0;
//                l.cutoff      = glm::radians(light.outerConeAngle.value());
//                l.innerCutoff = glm::radians(light.innerConeAngle.value());
//
//                l.radius = 100;
//                if (light.range.has_value())
//                    l.radius = light.range.value();
//                //spotLights.emplace_back(sl);
//                break;
//            case fastgltf::LightType::Directional:
//                l.color = glm::vec4(light.color.x(), light.color.y(), light.color.z(), 1);
//                //dl.lightDir = glm::fquat(nodeData.rotation.w(), nodeData.rotation.x(), nodeData.rotation.y(), nodeData.rotation.z());
//                //dirLights.emplace_back(dl);
//                break;
//            }
//        }
//    }
//}
