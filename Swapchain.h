#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

// The amount of images directly corresponds to the buffering method (2 = double buffering, 3 = triple buffering).
constexpr uint32_t IMAGE_COUNT = 2;

class Swapchain
{
public:
	Swapchain();
	Swapchain(vk::Device* device, vk::PhysicalDevice& pDevice, vk::SurfaceKHR& surface);
	vk::SwapchainKHR Get();

	void Recreate(SDL_Window* pWindow, bool vsync);

	vk::SwapchainKHR swapchain;

	vk::SurfaceKHR surface;
	vk::Extent2D renderExtend;
	vk::Format depthFormat;
	vk::Format renderFormat;
	vk::ImageSubresourceRange subresourceRange;
	std::vector<vk::ImageView> imageViews;
	std::vector<vk::Image> images;
private:
	void CreateImageViews();
	void Cleanup();

	vk::Device* pDevice;
	vk::ColorSpaceKHR colorSpace;
};