#include "Swapchain.h"

#include <iostream>

Swapchain::Swapchain() {

}

Swapchain::Swapchain(vk::Device* device, vk::PhysicalDevice& physicalDevice, vk::SurfaceKHR& surface) : surface(surface) {
    // Get surface capabilities.
    auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    auto surfacePresentModes = physicalDevice.getSurfacePresentModesKHR(surface);
    auto surfaceFormats      = physicalDevice.getSurfaceFormatsKHR(surface);

    bool supportsFifo = false;
    for (auto p : surfacePresentModes) {
        if (p == vk::PresentModeKHR::eFifo) {
            supportsFifo = true;
            break;
        }
    }
    if (!supportsFifo) std::runtime_error("No monitor found that supports FIFO, please ensure a monitor is connected to the GPU.");

    bool supportsSRGBformat = false;
    renderFormat = vk::Format::eR8G8B8A8Srgb;
    for (auto f : surfaceFormats) {
        if (f.format == vk::Format::eR8G8B8A8Srgb) {
            colorSpace = f.colorSpace;
            supportsSRGBformat = true;
            break;
        }
    }
    if (!supportsSRGBformat) std::runtime_error("No monitor found that supports sRGB, please ensure a monitor is connected to the GPU.");

    renderExtend = surfaceCapabilities.maxImageExtent;

    vk::SwapchainCreateInfoKHR swapchainInfo = vk::SwapchainCreateInfoKHR()
        .setSurface(surface)
        .setMinImageCount(IMAGE_COUNT)
        .setImageFormat(renderFormat)
        .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
        .setImageArrayLayers(1)
        .setImageColorSpace(colorSpace)
        .setImageExtent(renderExtend)
        .setPresentMode(vk::PresentModeKHR::eFifo);

    swapchain = device->createSwapchainKHR(swapchainInfo);
    images    = device->getSwapchainImagesKHR(swapchain);
    pDevice   = device;

    CreateImageViews();
}

vk::SwapchainKHR Swapchain::Get() {
    return swapchain;
}

void Swapchain::Cleanup() {
    for(auto &i : imageViews)
        pDevice->destroyImageView(i);
    pDevice->destroySwapchainKHR(swapchain);
}

void Swapchain::Recreate(SDL_Window* pWindow) {
    pDevice->waitIdle();
    Cleanup();

    int w, h;
    SDL_GetWindowSizeInPixels(pWindow, &w, &h);
    renderExtend = vk::Extent2D()
        .setHeight(h)
        .setWidth(w);

    vk::SwapchainCreateInfoKHR swapchainInfo = vk::SwapchainCreateInfoKHR()
        .setSurface(surface)
        .setMinImageCount(IMAGE_COUNT)
        .setImageFormat(renderFormat)
        .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
        .setImageArrayLayers(1)
        .setImageColorSpace(colorSpace)
        .setImageExtent(renderExtend)
        .setPresentMode(vk::PresentModeKHR::eFifo);

    swapchain = pDevice->createSwapchainKHR(swapchainInfo);
    images = pDevice->getSwapchainImagesKHR(swapchain);

    CreateImageViews();
}

void Swapchain::CreateImageViews() {
    subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);

    auto identity = vk::ComponentSwizzle::eIdentity;
    auto compMapping = vk::ComponentMapping()
        .setA(identity)
        .setB(identity)
        .setG(identity)
        .setR(identity);

    imageViews.clear();
    for (auto& i : images) {
        vk::ImageViewCreateInfo imageViewInfo = vk::ImageViewCreateInfo()
            .setComponents(compMapping)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(renderFormat)
            .setImage(i)
            .setSubresourceRange(subresourceRange);
        imageViews.push_back(pDevice->createImageView(imageViewInfo));
    }
}