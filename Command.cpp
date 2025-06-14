#include "Command.h"

Command::Command() {

}

Command::Command(Device& device, uint32_t cmdBufferCount) {
    pDevice = &device.device;

    // Command buffer allocation.
    vk::CommandPoolCreateInfo cmdPoolInfo = vk::CommandPoolCreateInfo()
        .setQueueFamilyIndex(device.graphicsQueueFamilyIndex)
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    cmdPool = device.device.createCommandPool(cmdPoolInfo);

    vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
        .setCommandBufferCount(cmdBufferCount)
        .setCommandPool(cmdPool)
        .setLevel(vk::CommandBufferLevel::ePrimary);
    auto cmdBuffers = device.device.allocateCommandBuffers(allocInfo);

    cmdBuffer = cmdBuffers;
}

void Command::TransitionImage(vk::Image& image, vk::ImageSubresourceRange& subresourceRange,
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
        .setImageMemoryBarrierCount(1)
        .setPImageMemoryBarriers(&imageMemoryBarrier);

    cmdBuffer[currentFrame].pipelineBarrier2(depencyInfo);
}

void Command::SetDynamicStates(vk::detail::DispatchLoaderDynamic& dldid) {
    cmdBuffer[currentFrame].setRasterizerDiscardEnable(vk::False);
    cmdBuffer[currentFrame].setDepthTestEnable(vk::False);
    cmdBuffer[currentFrame].setDepthWriteEnable(vk::False);
    cmdBuffer[currentFrame].setDepthCompareOp(vk::CompareOp::eAlways);
    cmdBuffer[currentFrame].setStencilTestEnable(vk::False);
    cmdBuffer[currentFrame].setDepthClampEnableEXT(vk::False, dldid);
    cmdBuffer[currentFrame].setDepthBiasEnable(vk::False);
    cmdBuffer[currentFrame].setPolygonModeEXT(vk::PolygonMode::eFill, dldid);
    cmdBuffer[currentFrame].setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1, dldid);
    uint32_t sampleMask = 1;
    cmdBuffer[currentFrame].setSampleMaskEXT(vk::SampleCountFlagBits::e1, sampleMask, dldid);
    cmdBuffer[currentFrame].setAlphaToCoverageEnableEXT(0, dldid);
    cmdBuffer[currentFrame].setCullMode(vk::CullModeFlagBits::eNone);
    cmdBuffer[currentFrame].setFrontFace(vk::FrontFace::eClockwise);
    cmdBuffer[currentFrame].setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);

    cmdBuffer[currentFrame].setPrimitiveRestartEnable(0);
    uint32_t colorBlendEnable = 1;
    cmdBuffer[currentFrame].setColorBlendEnableEXT(0, colorBlendEnable, dldid);
    cmdBuffer[currentFrame].setColorWriteMaskEXT(0, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA, dldid);
    auto colorBlendEquation = vk::ColorBlendEquationEXT()
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setDstColorBlendFactor(vk::BlendFactor::eZero)
        .setSrcColorBlendFactor(vk::BlendFactor::eOne);
    cmdBuffer[currentFrame].setColorBlendEquationEXT(0, colorBlendEquation, dldid);
}

void Command::SetCurrentFrame(uint32_t frame) {
    currentFrame = frame;
}