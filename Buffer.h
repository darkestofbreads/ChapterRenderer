#pragma once

#include <vulkan/vulkan.hpp>

class Buffer
{
public:
	Buffer();
	Buffer(vk::Device* device, vk::PhysicalDevice& pDevice, vk::BufferUsageFlags usage, vk::DeviceSize size, const void* source);
	vk::Buffer buffer;

private:
	vk::Device* device;
	vk::DeviceMemory memory;
};

uint32_t FindMemoryType(vk::PhysicalDevice pDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);