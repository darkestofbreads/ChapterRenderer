#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

class Shader
{
};

std::vector<uint32_t> ReadSPIRVFile(const char* fileName);
std::vector<vk::ShaderEXT> MakeMeshShaderObjects(vk::Device& device, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV,
 vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, vk::DescriptorSetLayout& setLayout);
std::vector<vk::ShaderEXT> MakeFallbackShaderObjects(vk::Device& device, const char* vertexFileNameSPIRV, const char* fragmentFileNameSPIRV, vk::detail::DispatchLoaderDynamic& dl);