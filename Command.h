#pragma once

#include "Device.h"

class Command
{
public:
	Command();
	Command(Device& device, uint32_t cmdBufferCount = 2);
	void Present(vk::PresentInfoKHR info, vk::Queue& queue, vk::Fence& renderFinished);
	void TransitionImage(vk::Image& image, vk::ImageSubresourceRange& subresourceRange,
		vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
		vk::AccessFlags2 dstMask, vk::AccessFlags2 srcMask);
	void SetDynamicStates(vk::detail::DispatchLoaderDynamic& dldid);
	void SetCurrentFrame(uint32_t frame);

	std::vector<vk::CommandBuffer> cmdBuffer;
	vk::CommandPool cmdPool;
private:
	vk::Device* pDevice;
	uint32_t currentFrame = 0;
};

