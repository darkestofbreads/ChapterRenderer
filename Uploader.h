#pragma once
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
};

template<class T> concept ContiguousRange = std::ranges::contiguous_range<T>;

class Uploader {
public:
	Uploader(VmaAllocator Allocator);


	template<ContiguousRange T>
	void StageUpload(vk::Buffer& dstBuffer, T data, size_t offset = 0) {
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
	void StageUpload(vk::Buffer& dstBuffer, T& data, size_t offset = 0) {
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
	void StageUpload(AllocatedBuffer& dstBuffer, T& data, size_t offset = 0) {
		using R = std::ranges::range_value_t<T>;
		StageUpload(dstBuffer.buffer, std::span<R>(data), offset);
	}
	template<class T> requires (!ContiguousRange<T>)
	void StageUpload(AllocatedBuffer& dstBuffer, T& data, size_t offset = 0) {
		StageUpload(dstBuffer.buffer, data, offset);
	}

	// The buffer returned has to be manually deleted by the application.
	AllocatedBuffer Upload(vk::CommandBuffer cmbBuffer, vk::Device device);
private:
	struct UploadInfo {
		vk::Buffer buffer;
		const void* data;
		size_t dstSize;
		size_t dstOffset;
		size_t stageBufferOffset;
	};

	VmaAllocator allocator;
	std::vector<UploadInfo> uploads;
	size_t stageBufferSize = 0;
};

AllocatedBuffer CreateAllocatedBuffer(vk::Device device, VmaAllocator allocator, size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage);