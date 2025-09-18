#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>

struct AllocatedBuffer {
	vk::Buffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo info;
};
struct AllocatedImage {
	vk::Image image;
	vk::ImageView view;
	VmaAllocation alloc;
};
struct GPUBuffer {
	AllocatedBuffer buffer;
	vk::DeviceAddress bufferAddress;
};