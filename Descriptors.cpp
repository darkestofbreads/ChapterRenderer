#include "Descriptors.h"

void Descriptors::AddBufferDescriptor(const vk::Buffer buffer, const vk::DeviceSize bufferSize,
        const vk::DescriptorType descriptorType, const uint32_t shaderBinding, void* pNext) {
    BufferDescriptor bufferDescriptor;
    bufferDescriptor.bufferSize = bufferSize;
    bufferDescriptor.buffer = buffer;
    bufferDescriptor.descriptorBufferInfos = std::vector<vk::DescriptorBufferInfo>(1);
    bufferDescriptor.descriptorBufferInfos[0] = vk::DescriptorBufferInfo()
    .setBuffer(buffer)
    .setRange(bufferSize);

    Descriptor descriptor;
    descriptor.descriptorType = descriptorType;
    descriptor.shaderBinding = shaderBinding;
    descriptor.bufferDescriptor = bufferDescriptor;
    descriptor.pNext = pNext;

    descriptors.emplace_back(descriptor);
}

void Descriptors::AddImageDescriptor(const std::vector<AllocatedImage> &images, const vk::ImageLayout layout, const uint32_t shaderBinding) {
    ImageDescriptor imageDescriptor;
    imageDescriptor.descriptorImageInfos = std::vector<vk::DescriptorImageInfo>(images.size());
    for (int i = 0; i < images.size(); i++) {
        imageDescriptor.descriptorImageInfos[i] = vk::DescriptorImageInfo()
        .setImageLayout(layout)
        .setSampler(images[i].sampler)
        .setImageView(images[i].view);
    }
    Descriptor descriptor;
    descriptor.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptor.shaderBinding = shaderBinding;
    descriptor.imageDescriptor = imageDescriptor;

    descriptors.emplace_back(descriptor);
}

void Descriptors::AddImageDescriptor(const AllocatedImage &image, const vk::ImageLayout layout, uint32_t shaderBinding) {
    ImageDescriptor imageDescriptor;
    imageDescriptor.descriptorImageInfos = std::vector<vk::DescriptorImageInfo>(1);
        imageDescriptor.descriptorImageInfos[0] = vk::DescriptorImageInfo()
        .setImageLayout(layout)
        .setSampler(image.sampler)
        .setImageView(image.view);

    Descriptor descriptor;
    descriptor.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptor.shaderBinding = shaderBinding;
    descriptor.imageDescriptor = imageDescriptor;

    descriptors.emplace_back(descriptor);
}

void Descriptors::UpdateImageDescriptor(const AllocatedImage &image, const vk::ImageLayout layout, uint32_t shaderBinding)
{
    ImageDescriptor imageDescriptor;
    imageDescriptor.descriptorImageInfos = std::vector<vk::DescriptorImageInfo>(1);
    imageDescriptor.descriptorImageInfos[0] = vk::DescriptorImageInfo()
        .setImageLayout(layout)
        .setSampler(image.sampler)
        .setImageView(image.view);

    Descriptor descriptor;
    descriptor.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptor.shaderBinding = shaderBinding;
    descriptor.imageDescriptor = imageDescriptor;

    for (auto &d : descriptors)
        if (d.shaderBinding == shaderBinding)
            d = descriptor;
}

void Descriptors::CreateSetsAndWriteDescriptors(const vk::Device device) {
    std::vector<vk::WriteDescriptorSet> descWrite(descriptors.size());
    std::vector<vk::DescriptorPoolSize> descPoolSizes(descriptors.size());
    std::vector<vk::DescriptorSetLayoutBinding> layoutBindings(descriptors.size());
    for (int i = 0; i < descriptors.size(); i++) {
        const auto& d = descriptors[i];
        size_t descriptorCount = 0;
        if (d.descriptorType == vk::DescriptorType::eCombinedImageSampler)
            descriptorCount = d.imageDescriptor.descriptorImageInfos.size();
        else
            descriptorCount = d.bufferDescriptor.descriptorBufferInfos.size();

        // Write descriptors
        descWrite[i] = vk::WriteDescriptorSet()
        .setDescriptorType(d.descriptorType)
        .setDstBinding(d.shaderBinding)
        .setDescriptorCount(descriptorCount)
        .setPNext(d.pNext);
        if (d.descriptorType == vk::DescriptorType::eCombinedImageSampler) {
            descWrite[i]
            .setImageInfo(d.imageDescriptor.descriptorImageInfos);
        } else {
            descWrite[i]
            .setBufferInfo(d.bufferDescriptor.descriptorBufferInfos);
        }
        // Descriptor pools
        descPoolSizes[i] = vk::DescriptorPoolSize()
        .setDescriptorCount(descriptorCount)
        .setType(d.descriptorType);
        // Layout bindings
        layoutBindings[i] = vk::DescriptorSetLayoutBinding()
        .setStageFlags(vk::ShaderStageFlagBits::eAll)
        .setDescriptorCount(descriptorCount)
        .setBinding(d.shaderBinding)
        .setDescriptorType(d.descriptorType);
    }

    const auto descriptorLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
            .setBindings(layoutBindings);
    descriptorLayout = device.createDescriptorSetLayout(descriptorLayoutInfo);
    const auto descPoolInfo = vk::DescriptorPoolCreateInfo()
        .setMaxSets(1)
        .setPoolSizes(descPoolSizes);
    const auto descAlloc = vk::DescriptorSetAllocateInfo()
        .setDescriptorPool(device.createDescriptorPool(descPoolInfo))
        .setSetLayouts(descriptorLayout);
    descriptorSets = device.allocateDescriptorSets(descAlloc);

    for (auto& w : descWrite) {
        w.setDstSet(descriptorSets[0]);
    }
    device.updateDescriptorSets(descWrite, nullptr);
}
