#pragma once
#include <memory>
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>

#include <vector>

struct AllocatedBuffer {
	vk::Buffer buffer;
	vk::DeviceAddress address;
	VmaAllocation alloc;
	VmaAllocationInfo info;
	bool isEmpty = true;
	size_t size = 0;
};
struct AllocatedImage {
	vk::Image image;
	vk::ImageView view;
	vk::Sampler sampler;
	VmaAllocation alloc;
	vk::Extent2D extent;
};

template<class T> concept ContiguousRange = std::ranges::contiguous_range<T>;
template<class T> concept ByteRange = std::ranges::contiguous_range<T> && std::is_same_v<std::ranges::range_value_t<T>, std::byte>;

class Uploader {
public:
	Uploader(VmaAllocator Allocator);

	template<ContiguousRange T>
	void StageUpload(vk::Buffer& dstBuffer, T data, const size_t offset = 0) {
		if (data.empty())
			return;
		
		UploadInfo upload;
		upload.data = data.data();

		upload.buffer = dstBuffer;
		upload.dstSize = data.size() * sizeof(std::ranges::range_value_t<T>);
		upload.dstOffset = offset;
		
		upload.stageBufferOffset = stageBufferSize;
		stageBufferSize += upload.dstSize;
		
		uploads.emplace_back(upload);
	}
	template<class T> requires(!ContiguousRange<T>)
	void StageUpload(vk::Buffer& dstBuffer, T& data, const size_t offset = 0) {
		UploadInfo upload;
		upload.data = &data;

		upload.buffer = dstBuffer;
		upload.dstSize = sizeof(T);
		upload.dstOffset = offset;

		upload.stageBufferOffset = stageBufferSize;
		stageBufferSize += upload.dstSize;

		uploads.emplace_back(upload);
	}

	template<ContiguousRange T>
	void StageUpload(AllocatedBuffer& dstBuffer, T& data, const size_t offset = 0) {
		using R = std::ranges::range_value_t<T>;
		StageUpload(dstBuffer.buffer, std::span<R>(data), offset);
	}
	template<class T> requires (!ContiguousRange<T>)
	void StageUpload(AllocatedBuffer& dstBuffer, T& data, const size_t offset = 0) {
		StageUpload(dstBuffer.buffer, data, offset);
	}
	void StageUpload(AllocatedImage& dstImage, const std::shared_ptr<void>& data, const size_t dataSize, const size_t offset = 0) {
		UploadInfo upload;
		upload.data = data;

		upload.buffer = dstImage.image;
		upload.dstSize = dataSize;
		upload.dstOffset = offset;
		upload.imageExtent = dstImage.extent;

		upload.stageBufferOffset = stageBufferSize;
		stageBufferSize += upload.dstSize;

		uploads.emplace_back(upload);
	}
	void StageUpload(AllocatedImage& dstImage, const void* data, const size_t dataSize, const size_t offset = 0) {
		UploadInfo upload;
		upload.data = data;

		upload.buffer = dstImage.image;
		upload.dstSize = dataSize;
		upload.dstOffset = offset;
		upload.imageExtent = dstImage.extent;

		upload.stageBufferOffset = stageBufferSize;
		stageBufferSize += upload.dstSize;

		uploads.emplace_back(upload);
	}

	// The buffer returned has to be manually deleted by the application.
	// Must be called while a command buffer is recording.
	[[nodiscard]] AllocatedBuffer Upload(vk::CommandBuffer cmbBuffer, vk::Device device) const;
private:
	struct UploadInfo {
		std::variant<vk::Buffer, vk::Image> buffer;
		std::variant<std::shared_ptr<void>, const void*> data;
		size_t dstSize{};
		size_t dstOffset{};
		size_t stageBufferOffset{};
		vk::Extent2D imageExtent;
	};

	VmaAllocator allocator;
	std::vector<UploadInfo> uploads;
	size_t stageBufferSize = 0;
};

[[nodiscard]] AllocatedBuffer CreateAllocatedBuffer(vk::Device device, VmaAllocator allocator, size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage);
[[nodiscard]] AllocatedImage CreateAllocatedImage(vk::Device device, VmaAllocator allocator, uint32_t width, uint32_t height, vk::Format format, vk::ImageUsageFlags usage, vk::Sampler sampler, bool makeMipmaps = false);
[[nodiscard]] AllocatedImage CreateAllocatedImage(vk::Device device, VmaAllocator allocator, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage, vk::Sampler sampler, bool makeMipmaps = false);
void InitializeImage(const vk::CommandBuffer& cmdBuffer, const vk::Image& image, vk::AccessFlags2 dstMask);