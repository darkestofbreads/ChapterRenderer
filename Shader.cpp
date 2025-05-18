#include "Shader.h"

#include <fstream>
#include <iostream>



std::vector<uint32_t> ReadSPIRVFile(const char* fileName) {
    std::ifstream shader(fileName, std::ios::ate | std::ios::binary);
    size_t size = static_cast<size_t>(shader.tellg());

    std::vector<uint32_t> data(size / 4);

    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(data.data()), size);
    shader.close();
    return data;
}

std::vector<vk::ShaderEXT> MakeMeshShaderObjects(vk::Device& device, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV,
    vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, vk::DescriptorSetLayout& setLayout) {
    std::vector<uint32_t> meshData = ReadSPIRVFile(meshShaderFileNameSPIRV);
    std::vector<uint32_t> fragData = ReadSPIRVFile(fragmentFileNameSPIRV);

    auto meshInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eNoTaskShader)
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
    shaders.push_back(meshShader.value);
    shaders.push_back(fragShader.value);
    return shaders;
}

std::vector<vk::ShaderEXT> MakeFallbackShaderObjects(vk::Device& device, const char* vertexFileNameSPIRV, const char* fragmentFileNameSPIRV, vk::detail::DispatchLoaderDynamic& dl) {
    std::vector<uint32_t> vertData = ReadSPIRVFile(vertexFileNameSPIRV);
    std::vector<uint32_t> fragData = ReadSPIRVFile(fragmentFileNameSPIRV);

    auto vertexInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setNextStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCodeSize(vertData.size() * sizeof(uint32_t))
        .setPCode(vertData.data())
        .setPName("main");
    auto fragmentInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCodeSize(fragData.size() * sizeof(uint32_t))
        .setPCode(fragData.data())
        .setPName("main");

    std::vector<vk::ShaderCreateInfoEXT> shaderInfos;
    shaderInfos.push_back(vertexInfo);
    shaderInfos.push_back(fragmentInfo);
    auto shaders = device.createShadersEXT(shaderInfos, nullptr, dl);

    if (shaders.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create shaders");
    }

    return shaders.value;
}