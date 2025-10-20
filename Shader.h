#pragma once
#include <vulkan/vulkan.hpp>
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>

#include <fstream>
#include <iostream>

#include <vector>
#include <span>

template<class T>
std::vector<T> ReadShaderFile(const char* fileName);
void DiagnoseSlang(slang::IBlob* diagnosticsBlob);
std::string CompileSlangToSPIRV(std::string computeShaderFileNameSlang);

vk::ShaderEXT MakeComputeShaderObject(vk::Device& device, const char* computeShaderFileNameSPIRV, vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);
vk::ShaderEXT MakeComputeShaderObjectSlang(vk::Device& device, const char* computeShaderFileNameSlang, vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjects(vk::Device& device,
    const char* taskShaderFileNameSPIRV, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV, vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);