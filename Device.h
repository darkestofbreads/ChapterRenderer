#pragma once

//#define VMA_IMPLEMENTATION

#include <vulkan/vulkan.hpp>

class Device
{
public:
	Device();
	Device(vk::Instance& instance);

	vk::Device device;
	vk::PhysicalDevice physicalDevice;

	uint32_t graphicsQueueFamilyIndex;
	uint32_t computeQueueFamilyIndex;

private:
};