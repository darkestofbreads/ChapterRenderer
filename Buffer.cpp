#include "Buffer.h"

Buffer::Buffer() {

}

Buffer::Buffer(vk::Device* device, vk::PhysicalDevice& pDevice, vk::BufferUsageFlags usage, vk::DeviceSize size, const void* source) {
    auto bufferInfo = vk::BufferCreateInfo()
        .setSize(size)
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive);
    buffer = device->createBuffer(bufferInfo);

    auto memoryRequirement = device->getBufferMemoryRequirements(buffer);
    auto memoryAllocInfo = vk::MemoryAllocateInfo()
        .setAllocationSize(memoryRequirement.size)
        .setMemoryTypeIndex(FindMemoryType(pDevice, memoryRequirement.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

    memory = device->allocateMemory(memoryAllocInfo);
    device->bindBufferMemory(buffer, memory, 0);

    auto mappedMemory = device->mapMemory(memory, 0, size);
    memcpy(mappedMemory, source, static_cast<size_t>(size));
    device->unmapMemory(memory);
}

uint32_t FindMemoryType(vk::PhysicalDevice pDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    auto memProperties = pDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (typeFilter & (1 << i))
            if ((memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
    }
}