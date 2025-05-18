#include "Instance.h"

#include <iostream>

Instance::Instance() {

}

Instance::Instance(SDL_Window* window, std::atomic<bool>* ready) {

    // Get WSI (Window System Integration) extensions from SDL.
    unsigned extensionCount;
    auto instanceExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

    // Use validation layers if this is a debug build.
    std::vector<const char*> layers;
#if defined(_DEBUG)
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    vk::ApplicationInfo appInfo = vk::ApplicationInfo()
        .setPApplicationName("Chapter One")
        .setApplicationVersion(1)
        .setPEngineName("Chapter Renderer Alpha")
        .setEngineVersion(1)
        .setApiVersion(VK_API_VERSION_1_4);

    vk::InstanceCreateInfo instInfo = vk::InstanceCreateInfo()
        .setFlags(vk::InstanceCreateFlags())
        .setPApplicationInfo(&appInfo)
        .setEnabledExtensionCount(static_cast<uint32_t>(extensionCount))
        .setPpEnabledExtensionNames(instanceExtensions)
        .setEnabledLayerCount(static_cast<uint32_t>(layers.size()))
        .setPEnabledLayerNames(layers);

    try {
        instance = vk::createInstance(instInfo);
    }
    catch (const std::exception& e) {
        std::cout << "Could not create a Vulkan instance: " << e.what() << std::endl;
    }

    // Create a Vulkan surface for rendering.
    VkSurfaceKHR cSurface;
    if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, &cSurface)) {
        std::cout << "Could not create a Vulkan surface." << std::endl;
    }
    surface = vk::SurfaceKHR(cSurface);

    pWindow = window;
    *ready = true;
}