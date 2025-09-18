#pragma once
#include "Shader.h"

std::vector<uint32_t> ReadSPIRVFile(const char* fileName) {
    std::ifstream shader(fileName, std::ios::ate | std::ios::binary);
    size_t size = static_cast<size_t>(shader.tellg());

    std::vector<uint32_t> data(size / 4);

    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(data.data()), size);
    shader.close();
    return data;
}

vk::ShaderEXT MakeComputeShaderObject(vk::Device& device, const char* computeShaderFileNameSPIRV,
    vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {
    std::vector<uint32_t> computeData = ReadSPIRVFile(computeShaderFileNameSPIRV);

    auto computeInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eDispatchBase)
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(computeData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);

    auto computeShader = device.createShaderEXT(computeInfo, nullptr, dl);
    if (computeShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create mesh shader\n";
        throw std::runtime_error("Failed to create mesh shader");
    }

    return computeShader.value;
}

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjects(vk::Device& device,
    const char* taskShaderFileNameSPIRV, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV,
    vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {

    std::vector<uint32_t> taskData = ReadSPIRVFile(taskShaderFileNameSPIRV);
    std::vector<uint32_t> meshData = ReadSPIRVFile(meshShaderFileNameSPIRV);
    std::vector<uint32_t> fragData = ReadSPIRVFile(fragmentFileNameSPIRV);

    auto taskInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eTaskEXT)
        .setNextStage(vk::ShaderStageFlagBits::eMeshEXT)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(taskData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);
    auto meshInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eMeshEXT)
        .setNextStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(meshData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);
    auto fragmentInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(fragData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);

    auto taskShader = device.createShaderEXT(taskInfo, nullptr, dl);
    if (taskShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create task shader\n";
        throw std::runtime_error("Failed to create task shader");
    }

    auto meshShader = device.createShaderEXT(meshInfo, nullptr, dl);
    if (meshShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create mesh shader\n";
        throw std::runtime_error("Failed to create mesh shader");
    }

    auto fragShader = device.createShaderEXT(fragmentInfo, nullptr, dl);
    if (fragShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create fragment shader\n";
        throw std::runtime_error("Failed to create fragment shader");
    }

    std::vector<vk::ShaderEXT> shaders;
    shaders.push_back(nullptr);
    shaders.push_back(taskShader.value);
    shaders.push_back(meshShader.value);
    shaders.push_back(fragShader.value);
    return shaders;
}