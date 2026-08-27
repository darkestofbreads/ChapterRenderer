#ifndef CHAPTERONE_DESCRIPTORS_H
#define CHAPTERONE_DESCRIPTORS_H

#include "Uploader.h"

#include <vector>
#include <vulkan/vulkan.hpp>

struct ImageDescriptor {
    std::vector<vk::DescriptorImageInfo> descriptorImageInfos;
};
struct BufferDescriptor {
    std::vector<vk::DescriptorBufferInfo> descriptorBufferInfos;
    vk::Buffer buffer;
    vk::DeviceSize bufferSize;
};
struct Descriptor {
    vk::DescriptorType descriptorType = vk::DescriptorType::eStorageBuffer;
    uint32_t shaderBinding = 0;

    ImageDescriptor imageDescriptor;
    BufferDescriptor bufferDescriptor;

    void* pNext = nullptr;
};

class Descriptors {
public:
    void AddImageDescriptor(const AllocatedImage &images, vk::ImageLayout layout, uint32_t shaderBinding);
    void UpdateImageDescriptor(const AllocatedImage& images, vk::ImageLayout layout, uint32_t shaderBinding);
    void AddImageDescriptor(const std::vector<AllocatedImage> &images, vk::ImageLayout layout, uint32_t shaderBinding);
    void AddBufferDescriptor(vk::Buffer buffer, vk::DeviceSize bufferSize, vk::DescriptorType descriptorType,
        uint32_t shaderBinding, void* pNext = nullptr);

    void CreateSetsAndWriteDescriptors(vk::Device device);

    std::vector<vk::DescriptorSet> descriptorSets;
    vk::DescriptorSetLayout descriptorLayout;
private:
    std::vector<Descriptor> descriptors;
};

#endif //CHAPTERONE_DESCRIPTORS_H
