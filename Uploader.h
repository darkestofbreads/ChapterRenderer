#pragma once
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>

#include <vector>

struct AllocatedBuffer {
	vk::Buffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo info;
	bool isEmpty = true;
};
struct AllocatedImage {
	vk::Image image;
	vk::ImageView view;
	VmaAllocation alloc;
};

struct GPUBuffer {
	vk::Buffer buffer;
	vk::DeviceAddress address;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

class Uploader {
public:
	Uploader(VmaAllocator Allocator);

	template<class T>
	void StageUpload(vk::Buffer& dstBuffer, std::span<T> data, size_t offset = 0) {
		if (data.empty())
			return;
		
		UploadInfo upload;
		upload.data = data.data();

		upload.buffer = dstBuffer;
		upload.dstSize = data.size() * sizeof(T);
		upload.dstOffset = offset;
		
		upload.stageBufferOffset = stageBufferSize;
		stageBufferSize += upload.dstSize;
		
		uploads.emplace_back(upload);
	}
	template<class T>
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

	template<class T>
	void StageUpload(GPUBuffer& dstBuffer, std::span<T> data, size_t offset = 0) {
		StageUpload(dstBuffer.buffer, data, offset);
	}
	template<class T>
	void StageUpload(GPUBuffer& dstBuffer, T& data, size_t offset = 0) {
		StageUpload(dstBuffer.buffer, data, offset);
	}

	// The buffer returned has to be manually deleted by the application.
	AllocatedBuffer Upload(vk::CommandBuffer cmbBuffer);
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

AllocatedBuffer CreateAllocatedBuffer(VmaAllocator allocator, size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage);