#pragma once
#include <vulkan/vulkan.hpp>

#include <fstream>
#include <iostream>

#include <vector>
#include <span>

std::vector<uint32_t> ReadSPIRVFile(const char* fileName);

vk::ShaderEXT MakeComputeShaderObject(vk::Device& device, const char* computeShaderFileNameSPIRV, vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);
std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjects(vk::Device& device,
    const char* taskShaderFileNameSPIRV, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV, vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);