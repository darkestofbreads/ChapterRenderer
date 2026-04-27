#include "Uploader.h"

Uploader::Uploader(VmaAllocator Allocator) {
    allocator = Allocator;
    uploads = std::vector<UploadInfo>();
}

AllocatedBuffer Uploader::Upload(vk::CommandBuffer cmbBuffer, vk::Device device) {
    AllocatedBuffer stageBuffer;
	if (uploads.empty())
		return stageBuffer;

    stageBuffer = CreateAllocatedBuffer(device, allocator, stageBufferSize, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_AUTO);
    auto byteData = static_cast<std::byte*>(stageBuffer.info.pMappedData);

    for (auto& u : uploads) {
        std::memcpy(&byteData[u.stageBufferOffset], u.data, u.dstSize);
    }

    for (auto& u : uploads) {
        auto region = vk::BufferCopy()
            .setSize(u.dstSize)
            .setSrcOffset(u.stageBufferOffset)
            .setDstOffset(u.dstOffset);
        cmbBuffer.copyBuffer(stageBuffer.buffer, u.buffer, region);
    }

    auto barrier = vk::MemoryBarrier2()
        .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
        .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
        .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
        .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite);
    auto depInfo = vk::DependencyInfo()
        .setMemoryBarriers(barrier);
    cmbBuffer.pipelineBarrier2(depInfo);

    stageBuffer.isEmpty = false;
    return stageBuffer;
}

AllocatedBuffer CreateAllocatedBuffer(vk::Device device, VmaAllocator allocator, size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage) {
    const auto use = usage | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    VkBufferCreateInfo bufferInfo = vk::BufferCreateInfo()
        .setSize(allocSize)
        .setUsage(use);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    AllocatedBuffer allocBuffer;
    VkBuffer buffer;
    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocBuffer.alloc, &allocBuffer.info);

    allocBuffer.buffer = vk::Buffer(buffer);
    auto addressInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(allocBuffer.buffer);
    allocBuffer.address = device.getBufferAddress(addressInfo);
    allocBuffer.size = allocSize;
    return allocBuffer;
}