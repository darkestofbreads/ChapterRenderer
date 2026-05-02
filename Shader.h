#pragma once
#include <vulkan/vulkan.hpp>
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>
#include <slang/slang-com-helper.h>

#include <fstream>
#include <iostream>

#include <vector>
#include <span>

//struct AccessPath
//{
//    AccessPathNode* leaf = nullptr;
//    AccessPathNode* deepestConstantBuffer = nullptr;
//    AccessPathNode* deepestParameterBlock = nullptr;
//};

template<class T>
std::vector<T> ReadShaderFile(const char* fileName);
void DiagnoseSlang(slang::IBlob* diagnosticsBlob);
std::string CompileSlangToSPIRV(std::string slangName);

vk::ShaderEXT MakeSingleShaderObjSlang(const vk::Device& device,
    const char* slangFileName, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout, vk::ShaderStageFlagBits stage, const std::string& entryPoint);
vk::ShaderEXT MakeSingleShaderObj(const vk::Device& device,
    const char* shaderFileNameSPIRV, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout, vk::ShaderStageFlagBits stage);

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjectsSlang(const vk::Device& device,
    const char* slangFileName, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);

vk::ShaderEXT MakeComputeShaderObject(const vk::Device& device, const char* computeShaderFileNameSPIRV, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);
vk::ShaderEXT MakeComputeShaderObjectSlang(const vk::Device& device, const char* slangFileName, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjects(const vk::Device& device,
    const char* taskShaderFileNameSPIRV, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout);