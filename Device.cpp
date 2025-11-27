#include "Device.h"

#include <iostream>

Device::Device() {

}

Device::Device(vk::Instance& instance) {
    // Create a physical device.
    auto pDevices = instance.enumeratePhysicalDevices();
    physicalDevice = pDevices[0];

    // Get support for extensions used in main render path.
    auto physicalExtensions = physicalDevice.enumerateDeviceExtensionProperties();

    // List of extensions used in main render path.
    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME); //
    deviceExtensions.push_back(VK_EXT_SHADER_OBJECT_EXTENSION_NAME); //
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME); //
    //deviceExtensions.push_back(VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME); //
    //deviceExtensions.push_back(VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
    std::vector<bool> extensionSupported(deviceExtensions.size());
    
    // Query support for main render path extensions.
    bool allExtensionsSupported = true;
    for (size_t i = 0; i < deviceExtensions.size(); i++) {
        bool isSupported = false;
        for (size_t j = 0; j < physicalExtensions.size(); j++)
            if (std::strcmp(deviceExtensions[i], physicalExtensions[j].extensionName) == 0) {
                extensionSupported[i] = true;
                isSupported = true;
            }
        if (!isSupported)
            allExtensionsSupported = false;
    }

    if (!allExtensionsSupported) {
        std::cout << "Your GPU does not support the following features:\n";
        for (size_t i = 0; i < deviceExtensions.size(); i++)
            if (!extensionSupported[i])
                std::cout << deviceExtensions[i] << std::endl;
    }

    // Chain of configured extension features.

    // GPU support not there yet.
    //auto fifoLatestReadyFeatures = vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR()
    //    .setPresentModeFifoLatestReady(vk::True);
    //auto unifiedImageFeatures = vk::PhysicalDeviceUnifiedImageLayoutsFeaturesKHR()
    //    .setUnifiedImageLayouts(vk::True)
    //    .setPNext(&fifoLatestReadyFeatures);

    // KHR version explicitly required for ImGui.
    auto dynamicRenderingFeaturesIMGUI = vk::PhysicalDeviceDynamicRenderingFeaturesKHR()
        .setDynamicRendering(vk::True);
        //.setPNext(&unifiedImageFeatures);
    auto descriptorIndexingFeatures = vk::PhysicalDeviceDescriptorIndexingFeatures()
        .setRuntimeDescriptorArray(vk::True)
        .setPNext(&dynamicRenderingFeaturesIMGUI);
    auto bufferDeviceAddressFeatures = vk::PhysicalDeviceBufferDeviceAddressFeatures()
        .setBufferDeviceAddress(vk::True)
        .setPNext(&descriptorIndexingFeatures);
    //auto dynamicRenderingFeatures = vk::PhysicalDeviceDynamicRenderingFeatures()
    //    .setDynamicRendering(vk::True)
    //    .setPNext(&bufferDeviceAddressFeatures);
    auto sync2Features = vk::PhysicalDeviceSynchronization2Features()
        .setSynchronization2(vk::True)
        .setPNext(&bufferDeviceAddressFeatures);
    auto shaderObjectFeatures = vk::PhysicalDeviceShaderObjectFeaturesEXT()
        .setShaderObject(vk::True)
        .setPNext(&sync2Features);
    auto meshShaderFeatures = vk::PhysicalDeviceMeshShaderFeaturesEXT()
        .setMeshShader(vk::True)
        .setTaskShader(vk::True)
        .setPNext(&shaderObjectFeatures);
    auto vulk14Features = vk::PhysicalDeviceVulkan14Features()
        .setPushDescriptor(vk::True)
        .setDynamicRenderingLocalRead(vk::True)
        .setPNext(&meshShaderFeatures);
    
    // Query queues and create infos.
    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    graphicsComputeQueueFamilyIndex = 0;
    for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
        if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute) {
                graphicsComputeQueueFamilyIndex = i;
                break;
            }
        }
    }

    float queuePriority = 1.0f;
    auto deviceQueueInfo = vk::DeviceQueueCreateInfo()
        .setQueueFamilyIndex(graphicsComputeQueueFamilyIndex)
        .setQueuePriorities(queuePriority);

    // Create a logical device.
    auto deviceInfo = vk::DeviceCreateInfo()
        .setPEnabledExtensionNames(deviceExtensions)
        .setQueueCreateInfos(deviceQueueInfo)
        .setPNext(&vulk14Features);

    device = physicalDevice.createDevice(deviceInfo);
}