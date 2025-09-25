#include "Command.h"

Command::Command() {

}

Command::Command(Device& device, uint32_t queueFamilyIndex) {
    pDevice = &device.device;

    // Command buffer allocation.
    vk::CommandPoolCreateInfo cmdPoolInfo = vk::CommandPoolCreateInfo()
        .setQueueFamilyIndex(queueFamilyIndex)
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    cmdPool = device.device.createCommandPool(cmdPoolInfo);

    vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
        .setCommandBufferCount(MAX_FRAMES_IN_FLIGHT)
        .setCommandPool(cmdPool)
        .setLevel(vk::CommandBufferLevel::ePrimary);
    auto cmdBuffers = device.device.allocateCommandBuffers(allocInfo);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        cmdBuffer[i] = cmdBuffers[i];
}

void TransitionImage(vk::CommandBuffer& cmdBuffer, vk::Image& image, vk::ImageSubresourceRange& subresourceRange,
                     vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                     vk::AccessFlags2 srcMask, vk::AccessFlags2 dstMask) {
    auto imageMemoryBarrier = vk::ImageMemoryBarrier2()
        .setImage(image)
        .setSubresourceRange(subresourceRange)
        .setNewLayout(newLayout)
        .setOldLayout(oldLayout)
        .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
        .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
        .setDstAccessMask(dstMask)
        .setSrcAccessMask(srcMask)
        .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands);
    auto depencyInfo = vk::DependencyInfo()
        .setImageMemoryBarriers(imageMemoryBarrier);

    cmdBuffer.pipelineBarrier2(depencyInfo);
}

void SetDynamicStates(vk::CommandBuffer& cmdBuffer, vk::detail::DispatchLoaderDynamic& dldid) {
    cmdBuffer.setRasterizerDiscardEnable(vk::False);
    cmdBuffer.setDepthTestEnable(vk::False);
    cmdBuffer.setDepthWriteEnable(vk::False);
    cmdBuffer.setDepthCompareOp(vk::CompareOp::eAlways);
    cmdBuffer.setStencilTestEnable(vk::False);
    cmdBuffer.setDepthClampEnableEXT(vk::False, dldid);
    cmdBuffer.setDepthBiasEnable(vk::False);
    cmdBuffer.setPolygonModeEXT(vk::PolygonMode::eFill, dldid);
    cmdBuffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1, dldid);
    uint32_t sampleMask = 1;
    cmdBuffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sampleMask, dldid);
    cmdBuffer.setAlphaToCoverageEnableEXT(0, dldid);
    cmdBuffer.setCullMode(vk::CullModeFlagBits::eNone);
    cmdBuffer.setFrontFace(vk::FrontFace::eClockwise);
    cmdBuffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);

    cmdBuffer.setPrimitiveRestartEnable(0);
    uint32_t colorBlendEnable = 1;
    cmdBuffer.setColorBlendEnableEXT(0, colorBlendEnable, dldid);
    cmdBuffer.setColorWriteMaskEXT(0, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA, dldid);
    auto colorBlendEquation = vk::ColorBlendEquationEXT()
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setDstColorBlendFactor(vk::BlendFactor::eZero)
        .setSrcColorBlendFactor(vk::BlendFactor::eOne);
    cmdBuffer.setColorBlendEquationEXT(0, colorBlendEquation, dldid);
}

std::array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> Command::GetCommandBuffers() {
    return cmdBuffer;
}