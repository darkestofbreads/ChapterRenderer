#include "Uploader.h"

#include <cmath>

Uploader::Uploader(VmaAllocator Allocator) {
    allocator = Allocator;
    uploads = std::vector<UploadInfo>();
}

AllocatedBuffer Uploader::Upload(const vk::CommandBuffer cmbBuffer, const vk::Device device) const {
    AllocatedBuffer stageBuffer{};
	if (uploads.empty())
		return stageBuffer;

    stageBuffer = CreateAllocatedBuffer(device, allocator, stageBufferSize, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_AUTO);
    const auto byteData = static_cast<std::byte*>(stageBuffer.info.pMappedData);

    for (auto& u : uploads) {
        if (std::holds_alternative<const void*>(u.data))
            std::memcpy(&byteData[u.stageBufferOffset], std::get<const void*>(u.data), u.dstSize);
        else if (std::holds_alternative<std::shared_ptr<void>>(u.data))
            std::memcpy(&byteData[u.stageBufferOffset], std::get<std::shared_ptr<void>>(u.data).get(), u.dstSize);
    }

    for (auto& u : uploads) {
        if (std::holds_alternative<vk::Buffer>(u.buffer)) {
            auto region = vk::BufferCopy()
            .setSize(u.dstSize)
            .setSrcOffset(u.stageBufferOffset)
            .setDstOffset(u.dstOffset);
            cmbBuffer.copyBuffer(stageBuffer.buffer, std::get<vk::Buffer>(u.buffer), region);
        } else if (std::holds_alternative<vk::Image>(u.buffer)) {
            constexpr auto imageSubresource = vk::ImageSubresourceLayers()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(1);
            auto region = vk::BufferImageCopy()
            .setBufferOffset(u.stageBufferOffset)
            .setImageOffset(u.dstOffset)
            .setBufferImageHeight(0)
            .setBufferRowLength(0)
            .setImageExtent(vk::Extent3D(u.imageExtent, 1))
            .setImageSubresource(imageSubresource);

            InitializeImage(cmbBuffer, std::get<vk::Image>(u.buffer), vk::AccessFlagBits2::eTransferWrite);
            cmbBuffer.copyBufferToImage(stageBuffer.buffer, std::get<vk::Image>(u.buffer), vk::ImageLayout::eGeneral, region);
        }
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

void InitializeImage(const vk::CommandBuffer& cmdBuffer, const vk::Image& image, const vk::AccessFlags2 dstMask) {
    constexpr auto subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);
    const auto imageMemoryBarrier = vk::ImageMemoryBarrier2()
        .setImage(image)
        .setSubresourceRange(subresourceRange)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
        .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
        .setDstAccessMask(dstMask)
        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
        .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands);
    const auto dependencyInfo = vk::DependencyInfo()
        .setImageMemoryBarriers(imageMemoryBarrier);
    cmdBuffer.pipelineBarrier2(dependencyInfo);
}

AllocatedBuffer CreateAllocatedBuffer(const vk::Device device, const VmaAllocator allocator, const size_t allocSize, const vk::Flags<vk::BufferUsageFlagBits> usage, const VmaMemoryUsage memUsage) {
    const auto use = usage | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    const VkBufferCreateInfo bufferInfo = vk::BufferCreateInfo()
        .setSize(allocSize)
        .setUsage(use);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    AllocatedBuffer allocBuffer;
    VkBuffer buffer;
    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocBuffer.alloc, &allocBuffer.info);

    allocBuffer.buffer = vk::Buffer(buffer);
    const auto addressInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(allocBuffer.buffer);
    allocBuffer.address = device.getBufferAddress(addressInfo);
    allocBuffer.size = allocSize;
    return allocBuffer;
}
AllocatedImage CreateAllocatedImage(const vk::Device device, const VmaAllocator allocator, const uint32_t width, const uint32_t height, const vk::Format format, const vk::ImageUsageFlags usage, const vk::Sampler sampler, const bool makeMipmaps) {
    AllocatedImage allocImage{};
    allocImage.extent = vk::Extent2D(width, height);
    uint32_t mipLevelCount = 1;
    if (makeMipmaps)
        mipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(height, width)))) + 1;

    const auto imageUsage = usage | vk::ImageUsageFlagBits::eTransferDst;
    auto imageInfo = vk::ImageCreateInfo()
        .setArrayLayers(1)
        .setExtent(vk::Extent3D(allocImage.extent, 1))
        .setFlags(vk::ImageCreateFlags())
        .setFormat(format)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setMipLevels(mipLevelCount)
        .setUsage(imageUsage)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setImageType(vk::ImageType::e2D)
        .setTiling(vk::ImageTiling::eOptimal)
        .setQueueFamilyIndices(nullptr);

    VmaAllocationCreateInfo imageAllocCreateInfo = {};
    imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    imageAllocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    {const auto result = vmaCreateImage(allocator, reinterpret_cast<VkImageCreateInfo*>(&imageInfo), &imageAllocCreateInfo, reinterpret_cast<VkImage*>(&allocImage.image), &allocImage.alloc, nullptr);}

    constexpr auto identity = vk::ComponentSwizzle::eIdentity;
    constexpr auto compMapping = vk::ComponentMapping()
        .setA(identity)
        .setB(identity)
        .setG(identity)
        .setR(identity);

    auto subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);
    if (imageUsage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
        subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eDepth);

    const auto imageViewInfo = vk::ImageViewCreateInfo()
        .setComponents(compMapping)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(format)
        .setImage(allocImage.image)
        .setSubresourceRange(subresourceRange);

    allocImage.view = device.createImageView(imageViewInfo);
    allocImage.sampler = sampler;
    return allocImage;
}
AllocatedImage CreateAllocatedImage(const vk::Device device, const VmaAllocator allocator, const vk::Extent2D extent, const vk::Format format, const vk::ImageUsageFlags usage, const vk::Sampler sampler, const bool makeMipmaps) {
    return CreateAllocatedImage(device, allocator, extent.width, extent.height, format, usage, sampler, makeMipmaps);
}