#ifndef CHAPTERONE_GLTF_H
#define CHAPTERONE_GLTF_H

#include "Uploader.h"

#include "stb_image.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <cstdint>
#include <vector>
#include <iostream>

uint32_t ParseGLTFImage(const fastgltf::TextureInfo& imageInfo, const fastgltf::Asset& asset, std::vector<AllocatedImage>& txtrs, Uploader& uploader, vk::Device device, VmaAllocator allocator, vk::Sampler sampler);

template<typename T>
std::vector<T> ReadGLTFAttribute(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, std::string_view Attribute) {
    const auto& iterator = primitive.findAttribute(Attribute);
    assert(iterator != nullptr);

    const auto& acr = asset.accessors[iterator->accessorIndex];
    const auto& bufferView = asset.bufferViews[acr.bufferViewIndex.value()];

    const auto& buffer = asset.buffers[bufferView.bufferIndex];
    const auto& data = get<fastgltf::sources::Array>(buffer.data);

    std::vector<T> out(acr.count);
    std::memcpy(out.data(), data.bytes.data() + bufferView.byteOffset + acr.byteOffset, acr.count * sizeof(T));
    return out;
}

struct MaterialIndexGroup {
    uint32_t diffuse;
    uint32_t metallicRoughness;
    uint32_t emissive;
    uint32_t pad;
};

class GLTF
{
public:
    GLTF(const std::filesystem::path& path);
    void LoadMaterials(std::vector<uint32_t> &materialIDs, std::vector<MaterialIndexGroup>& materialIndexGroups, std::vector<AllocatedImage>& textures, VmaAllocator allocator, vk::Device device, vk::Sampler sampler, Uploader& uploader);
    void LoadLights();
    fastgltf::Asset asset;

private:
};

#endif //CHAPTERONE_GLTF_H
