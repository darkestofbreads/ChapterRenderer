#pragma once
#include "Shader.h"

template<class T>
std::vector<T> ReadShaderFile(const char* fileName) {
    std::ifstream shader((std::string)(fileName), std::ios::ate | std::ios::binary);
    size_t size = static_cast<size_t>(shader.tellg());

    std::vector<T> data( size / (sizeof(T) / sizeof(char)) );

    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(data.data()), size);
    shader.close();
    return data;
}

void DiagnoseSlang(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
        std::cout << (const char*)diagnosticsBlob->getBufferPointer() << std::endl;
}
std::string CompileSlangToSPIRV(std::string slangName) {
    std::string moduleName;
    for (auto i = slangName.find('/') + 1; i < slangName.length(); i++) {
        auto c = slangName[i];
        if (c == '.')
            break;
        moduleName += c;
    }

    std::string shaderPath = std::format("shaders/{}.slang", moduleName);
    auto shaderRaw = ReadShaderFile<char>(shaderPath.c_str());
    std::string shader(shaderRaw.begin(), shaderRaw.end());

    // 1. Create Global Session
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    createGlobalSession(globalSession.writeRef());

    // 2. Create Session
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    std::array<slang::CompilerOptionEntry, 1> options =
    {
        {
            slang::CompilerOptionName::EmitSpirvDirectly,
            {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
        }
    };
    sessionDesc.compilerOptionEntries = options.data();
    sessionDesc.compilerOptionEntryCount = options.size();

    const char* includePath = "shaders";
    auto includePaths = &includePath;
    sessionDesc.searchPaths = includePaths;
    sessionDesc.searchPathCount = 1;

    Slang::ComPtr<slang::ISession> session;
    globalSession->createSession(sessionDesc, session.writeRef());

    // 3. Load module
    std::string spirvPath = std::format("shaders/compiled/{}.slang.spv", moduleName);
    Slang::ComPtr<slang::IModule> slangModule;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule = session->loadModuleFromSourceString(
            moduleName.c_str(),
            spirvPath.c_str(),
            shader.c_str(),
            diagnosticsBlob.writeRef());
        DiagnoseSlang(diagnosticsBlob);
        if (!slangModule)
        {
            std::cout << "Error loading module" << std::endl;
            return NULL;
        }
    }

    // 4. Query Entry Points
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule->findEntryPointByName("computeMain", entryPoint.writeRef());
        if (!entryPoint)
        {
            std::cout << "Error getting entry point" << std::endl;
            return NULL;
        }
    }

    // 5. Compose Modules + Entry Points
    std::array<slang::IComponentType*, 2> componentTypes =
    {
        slangModule,
        entryPoint
    };

    Slang::ComPtr<slang::IComponentType> composedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef());
    }

    // 6. Link
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = composedProgram->link(
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef());
    }

    // 7. Get Target Kernel Code
    Slang::ComPtr<slang::IBlob> spirvCode;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = linkedProgram->getEntryPointCode(
            0,
            0,
            spirvCode.writeRef(),
            diagnosticsBlob.writeRef());
    }

    std::cout << "Compiled " << spirvCode->getBufferSize() << " bytes of SPIR-V" << std::endl;
    
    const char* data = static_cast<const char*>(spirvCode->getBufferPointer());
    std::ofstream spirvOut(spirvPath, std::ios::binary);

    spirvOut.seekp(0);
    spirvOut.write(data, spirvCode->getBufferSize());
    spirvOut.close();
    return spirvPath;
}

vk::ShaderEXT MakeComputeShaderObject(vk::Device& device, const char* computeShaderFileNameSPIRV,
    vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {
    std::vector<uint32_t> computeData = ReadShaderFile<uint32_t>(computeShaderFileNameSPIRV);

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
        std::cout << "Failed to create compute shader\n";
        throw std::runtime_error("Failed to create compute shader");
    }

    return computeShader.value;
}

vk::ShaderEXT MakeComputeShaderObjectSlang(vk::Device& device, const char* slangFileName,
    vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {
    auto path = CompileSlangToSPIRV(slangFileName);

    std::vector<uint32_t> computeData = ReadShaderFile<uint32_t>(path.c_str());

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
        std::cout << "Failed to create compute shader\n";
        throw std::runtime_error("Failed to create compute shader");
    }

    return computeShader.value;
}

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjects(vk::Device& device,
    const char* taskShaderFileNameSPIRV, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV,
    vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {

    std::vector<uint32_t> taskData = ReadShaderFile<uint32_t>(taskShaderFileNameSPIRV);
    std::vector<uint32_t> meshData = ReadShaderFile<uint32_t>(meshShaderFileNameSPIRV);
    std::vector<uint32_t> fragData = ReadShaderFile<uint32_t>(fragmentFileNameSPIRV);

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