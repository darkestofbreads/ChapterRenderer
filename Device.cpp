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
    deviceExtensions.push_back(VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME); //
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME); //
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

    auto unusedAttachmentsFeatures = vk::PhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT()
        .setDynamicRenderingUnusedAttachments(vk::True);
    // KHR version explicitly required for ImGui.
    auto dynamicRenderingFeaturesIMGUI = vk::PhysicalDeviceDynamicRenderingFeaturesKHR()
        .setDynamicRendering(vk::True)
        .setPNext(&unusedAttachmentsFeatures);

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
    auto vulk14Features = vk::PhysicalDeviceVulkan14Features()
        .setPushDescriptor(vk::True)
        .setPNext(&shaderObjectFeatures);
    auto meshShaderFeatures = vk::PhysicalDeviceMeshShaderFeaturesEXT()
        .setMeshShader(vk::True)
        .setTaskShader(vk::True)
        .setPNext(&vulk14Features);

    // Query queues and create infos.
    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    graphicsQueueFamilyIndex = 0;
    computeQueueFamilyIndex = 0;
    for (size_t i = 0; i < queueFamilyProperties.size(); i++)
        if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphicsQueueFamilyIndex = i;
            break;
        }
    for (size_t i = 0; i < queueFamilyProperties.size(); i++)
        if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute) {
            if (i == graphicsQueueFamilyIndex)
                continue;
            computeQueueFamilyIndex = i;
            break;
        }

    vk::DeviceQueueCreateInfo deviceQueueInfo[2];
    float graphicsPrio = 1.0f, computePrio = 1.0f;
    deviceQueueInfo[0] = vk::DeviceQueueCreateInfo()
        .setQueueFamilyIndex(graphicsQueueFamilyIndex)
        .setQueuePriorities(graphicsPrio);
    deviceQueueInfo[1] = vk::DeviceQueueCreateInfo()
        .setQueueFamilyIndex(computeQueueFamilyIndex)
        .setQueuePriorities(computePrio);

    // Create a logical device.
    vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo()
        .setPEnabledExtensionNames(deviceExtensions)
        .setQueueCreateInfos(deviceQueueInfo)
        .setPNext(&meshShaderFeatures);

    device = physicalDevice.createDevice(deviceInfo);
}