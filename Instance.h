#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

class Instance
{
public:
	Instance();
	Instance(SDL_Window* window, std::atomic<bool>* ready);

	vk::Instance instance;
	vk::SurfaceKHR surface;
	SDL_Window* pWindow;
};

