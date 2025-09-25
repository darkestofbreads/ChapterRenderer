#pragma once

#include "Device.h"

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

class Command
{
public:
	Command();
	Command(Device& device, uint32_t queueFamilyIndex);

	std::array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> GetCommandBuffers();
	vk::CommandPool cmdPool;
private:
	vk::Device* pDevice;
	std::array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> cmdBuffer;
};

void SetDynamicStates(vk::CommandBuffer& cmdBuffer, vk::detail::DispatchLoaderDynamic& dldid);
void TransitionImage(vk::CommandBuffer& cmdBuffer, vk::Image& image, vk::ImageSubresourceRange& subresourceRange,
	vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
	vk::AccessFlags2 srcMask, vk::AccessFlags2 dstMask);