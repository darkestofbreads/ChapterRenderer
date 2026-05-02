#include "Shader.h"

template<class T>
std::vector<T> ReadShaderFile(const char* fileName) {
    std::ifstream shader(static_cast<std::string>(fileName), std::ios::ate | std::ios::binary);
    const size_t size = static_cast<size_t>(shader.tellg());

    std::vector<T> data( size / (sizeof(T) / sizeof(char)) );

    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(data.data()), size);
    shader.close();
    return data;
}

void DiagnoseSlang(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
        std::cout << static_cast<const char *>(diagnosticsBlob->getBufferPointer()) << std::endl;
}

/*CumulativeOffset calculateCumulativeOffset(slang::ParameterCategory layoutUnit, AccessPath accessPath)
{
    switch (layoutUnit)
    {
        case slang::ParameterCategory::ConstantBuffer:
        case slang::ParameterCategory::ShaderResource:
        case slang::ParameterCategory::UnorderedAccess:
        case slang::ParameterCategory::SamplerState:
        case slang::ParameterCategory::DescriptorTableSlot:
            for (auto node = accessPath.leaf; node != accessPath.deepestParameterBlock; node = node->outer)
            {
                result.offset += node->varLayout->getOffset(layoutUnit);
                result.space += node->varLayout->getBindingSpace(layoutUnit);
            }
            for (auto node = accessPath.deepestParameterBlock; node != nullptr; node = node->outer)
            {
                result.space += node->varLayout->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
            }
        break;
        case slang::ParameterCategory::Uniform:
            for (auto node = accessPath.leaf; node != accessPath.deepestConstantBuffer; node = node->outer)
            {
                result.offset += node->varLayout->getOffset(layoutUnit);
            }
        break;
        default:
            for (auto node = accessPath.leaf; node != nullptr; node = node->outer)
            {
                result.offset += node->varLayout->getOffset(layoutUnit);
            }
        break;
    }

    for (auto node = accessPath.leafNode; node != nullptr; node = node->outer)
    {
        result.value += node->varLayout->getOffset(layoutUnit);
        result.space += node->varLayout->getBindingSpace(layoutUnit);
    }
    // ...
}
void PrintCumulativeOffset(
    slang::VariableLayoutReflection* varLayout,
    slang::ParameterCategory layoutUnit,
    AccessPath accessPath)
{
    CumulativeOffset cumulativeOffset = calculateCumulativeOffset(layoutUnit, accessPath);

    cumulativeOffset.offset += varLayout->getOffset(layoutUnit);
    cumulativeOffset.space += varLayout->getBindingSpace(layoutUnit);

    PrintOffset(layoutUnit, cumulativeOffset.offset, cumulativeOffset.space);
}
void PrintOffsets(
    slang::VariableLayoutReflection* varLayout,
    AccessPath accessPath)
{
    // ...

    std::cout << "cumulative:";
    for (int i = 0; i < usedLayoutUnitCount; ++i)
    {
        std::cout << "- ";
        auto layoutUnit = varLayout->getCategoryByIndex(i);
        PrintCumulativeOffset(varLayout, layoutUnit, accessPath);
    }
}
void PrintTypeLayout(slang::TypeLayoutReflection* typeLayout, AccessPath accessPath)
{
    // ...
}
void PrintVarLayout(slang::VariableLayoutReflection* varLayout)
{
    // ...
    if (varLayout->getStage() != SLANG_STAGE_NONE)
    {
        std::cout << "semantic: ";
        std::cout << "name: "; printQuotedString(varLayout->getSemanticName());
        std::cout << "index: " << varLayout->getSemanticIndex();
    }

    ExtendedAccessPath varAccessPath(accessPath, varLayout);

    std::cout << "type layout: ";
    PrintTypeLayout(varLayout->getTypeLayout(), varAccessPath);
}
void PrintScope(slang::VariableLayoutReflection* scopeVarLayout)
{
    auto scopeTypeLayout = scopeVarLayout->getTypeLayout();
    switch (scopeTypeLayout->getKind())
    {
        case slang::TypeReflection::Kind::Struct: {
            std::cout << "parameters: ";

            int paramCount = scopeTypeLayout->getFieldCount();
            for (int i = 0; i < paramCount; i++)
            {
                std::cout << "- ";

                auto param = scopeTypeLayout->getFieldByIndex(i);
                PrintVarLayout(param);
            }
        }
        break;
        case slang::TypeReflection::Kind::ConstantBuffer: {
            std::cout << "automatically-introduced constant buffer: ";

            printOffsets(scopeTypeLayout->getContainerVarLayout());

            PrintScope(scopeTypeLayout->getElementVarLayout());
        }
        break;
        case slang::TypeReflection::Kind::ParameterBlock: {
            std::cout << "automatically-introduced parameter block: ";

            printOffsets(scopeTypeLayout->getContainerVarLayout());

            PrintScope(scopeTypeLayout->getElementVarLayout());
        }
        break;
        case slang::TypeReflection::Kind::ShaderStorageBuffer:
        {
            AccumulatedOffsets innerAccessPath = accessPath;
            innerAccessPath.deepestConstantBuffer = innerAccessPath.leaf;

            if (containerVarLayout->getTypeLayout()->getSize(
                slang::ParameterCategory::SubElementRegisterSpace) != 0)
            {
                innerAccessPath.deepestParameterBlock = innerAccessPath.leaf;
            }
            
            std::cout << "element: ";
            PrintOffsets(scopeTypeLayout, innerAccessPath);

            ExtendedAccessPath elementAccessPath(innerAccessPath, scopeTypeLayout);

            std::cout << "type layout: ";
            PrintTypeLayout(
                scopeTypeLayout,
                elementAccessPath);
        }
        break;
        case slang::TypeReflection::Kind::Array:
        {
            // ...

            std::cout << "element type layout: ";
            PrintTypeLayout(scopeTypeLayout->getElementTypeLayout(), AccessPath());
        }
        break;

    }
}
void PrintEntryPointLayout(slang::EntryPointReflection* entryPointLayout)
{
    std::cout << "stage: ";

    switch (entryPointLayout->getStage())
    {
        case SLANG_STAGE_COMPUTE:
        {
            SlangUInt sizes[3];
            entryPointLayout->getComputeThreadGroupSize(3, sizes);

            std::cout << "thread group size: ";
            std::cout << "x: "; std::cout << sizes[0];
            std::cout << "y: "; std::cout << sizes[1];
            std::cout << "z: "; std::cout << sizes[2];
        }
        break;
    }

    PrintScope(entryPointLayout->getVarLayout());

    auto resultVarLayout = entryPointLayout->getResultVarLayout();
    if (resultVarLayout->getTypeLayout()->getKind() != slang::TypeReflection::Kind::None) {
        key("result"); 
        PrintVarLayout(resultVarLayout);
    }
}
void ReflectShader(slang::ShaderReflection* layout) {
    std::cout << "global scope: ";
    PrintScope(layout->getGlobalParamsVarLayout());
    
}*/

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

    // Create Session.
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    //targetDesc.format = SLANG_GLSL;
    targetDesc.profile = globalSession->findProfile("spirv_1_6");

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    std::array<slang::CompilerOptionEntry, 3> options;
    options[0] = {slang::CompilerOptionName::EmitSpirvDirectly,       {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}};
    options[1] = {slang::CompilerOptionName::VulkanUseEntryPointName, {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}};
    options[2] = {slang::CompilerOptionName::BindlessSpaceIndex,      {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}};

    // CompilerOptionName::BindlessSpaceIndex
    sessionDesc.compilerOptionEntries = options.data();
    sessionDesc.compilerOptionEntryCount = options.size();

    auto includePath = "shaders";
    auto includePaths = &includePath;
    sessionDesc.searchPaths = includePaths;
    sessionDesc.searchPathCount = 1;

    Slang::ComPtr<slang::ISession> session;
    globalSession->createSession(sessionDesc, session.writeRef());

    // Load module.
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
            std::cout << "Error loading module" << std::endl;
    }

    // Link dependencies.
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = slangModule->link(
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef());
        DiagnoseSlang(diagnosticsBlob);
        if (!linkedProgram)
            std::cout << "Error linking dependencies" << std::endl;
    }

    // Get Target Kernel Code.
    Slang::ComPtr<slang::IBlob> spirvCode;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = linkedProgram->getTargetCode(
            0,
            spirvCode.writeRef(),
            diagnosticsBlob.writeRef()
        );
        DiagnoseSlang(diagnosticsBlob);
        if (!spirvCode)
            std::cout << "Error getting kernel code" << std::endl;
    }

    // Reflection.
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slang::ShaderReflection* layout = linkedProgram->getLayout(0, diagnosticsBlob.writeRef());
        DiagnoseSlang(diagnosticsBlob);
        //ReflectShader(layout);
    }

    std::cout << "Compiled " << spirvCode->getBufferSize() << " bytes of SPIR-V" << std::endl;
    
    const char* data = static_cast<const char*>(spirvCode->getBufferPointer());

    std::ofstream spirvOut(spirvPath, std::ios::binary);
    spirvOut.seekp(0);
    spirvOut.write(data, spirvCode->getBufferSize());
    spirvOut.close();

    return spirvPath;
}

vk::ShaderEXT MakeSingleShaderObjSlang(const vk::Device& device,
    const char* slangFileName, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout, const vk::ShaderStageFlagBits stage, const std::string& entryPoint) {
    const auto path = CompileSlangToSPIRV(slangFileName);
    std::vector<uint32_t> data = ReadShaderFile<uint32_t>(path.c_str());

    const auto info = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eDispatchBase)
        .setStage(stage)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(data)
        .setPName(entryPoint.c_str())
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);

    const auto computeShader = device.createShaderEXT(info, nullptr, dl);
    if (computeShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create shader\n";
        throw std::runtime_error("Failed to create shader");
    }

    return computeShader.value;
}

vk::ShaderEXT MakeSingleShaderObj(const vk::Device& device,
    const char* shaderFileNameSPIRV, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout, const vk::ShaderStageFlagBits stage) {
    std::vector<uint32_t> data = ReadShaderFile<uint32_t>(shaderFileNameSPIRV);

    const auto computeInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eDispatchBase)
        .setStage(stage)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(data)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);

    auto computeShader = device.createShaderEXT(computeInfo, nullptr, dl);
    if (computeShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create shader\n";
        throw std::runtime_error("Failed to create shader");
    }

    return computeShader.value;
}

vk::ShaderEXT MakeComputeShaderObject(const vk::Device& device, const char* computeShaderFileNameSPIRV,
    const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {
    std::vector<uint32_t> computeData = ReadShaderFile<uint32_t>(computeShaderFileNameSPIRV);

    const auto computeInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eDispatchBase)
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(computeData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);

    const auto computeShader = device.createShaderEXT(computeInfo, nullptr, dl);
    if (computeShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create compute shader\n";
        throw std::runtime_error("Failed to create compute shader");
    }

    return computeShader.value;
}

vk::ShaderEXT MakeComputeShaderObjectSlang(const vk::Device& device, const char* slangFileName,
    const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {
    const auto path = CompileSlangToSPIRV(slangFileName);
    std::vector<uint32_t> computeData = ReadShaderFile<uint32_t>(path.c_str());

    const auto computeInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eDispatchBase)
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(computeData)
        .setPName("computeMain")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);

    const auto computeShader = device.createShaderEXT(computeInfo, nullptr, dl);
    if (computeShader.result != vk::Result::eSuccess) {
        std::cout << "Failed to create compute shader\n";
        throw std::runtime_error("Failed to create compute shader");
    }

    return computeShader.value;
}

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjectsSlang(const vk::Device& device,
    const char* slangFileName, const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {
    const auto path = CompileSlangToSPIRV(slangFileName);
    std::vector<uint32_t> shaderData = ReadShaderFile<uint32_t>(path.c_str());

    const auto taskInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eTaskEXT)
        .setNextStage(vk::ShaderStageFlagBits::eMeshEXT)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(shaderData)
        .setPushConstantRanges(range)
        .setPName("taskMain")
        .setSetLayouts(setLayout);
    const auto meshInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eMeshEXT)
        .setNextStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(shaderData)
        .setPushConstantRanges(range)
        .setPName("meshMain")
        .setSetLayouts(setLayout);
    const auto fragmentInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(shaderData)
        .setPushConstantRanges(range)
        .setPName("fragMain")
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
    shaders.emplace_back(nullptr);
    shaders.emplace_back(taskShader.value);
    shaders.emplace_back(meshShader.value);
    shaders.emplace_back(fragShader.value);
    return shaders;
}

std::vector<vk::ShaderEXT> MakeTaskMeshShaderObjects(const vk::Device& device,
    const char* taskShaderFileNameSPIRV, const char* meshShaderFileNameSPIRV, const char* fragmentFileNameSPIRV,
    const vk::detail::DispatchLoaderDynamic& dl, vk::PushConstantRange& range, std::span<vk::DescriptorSetLayout> setLayout) {

    std::vector<uint32_t> taskData = ReadShaderFile<uint32_t>(taskShaderFileNameSPIRV);
    std::vector<uint32_t> meshData = ReadShaderFile<uint32_t>(meshShaderFileNameSPIRV);
    std::vector<uint32_t> fragData = ReadShaderFile<uint32_t>(fragmentFileNameSPIRV);

    const auto taskInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eTaskEXT)
        .setNextStage(vk::ShaderStageFlagBits::eMeshEXT)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(taskData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);
    const auto meshInfo = vk::ShaderCreateInfoEXT()
        .setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage)
        .setStage(vk::ShaderStageFlagBits::eMeshEXT)
        .setNextStage(vk::ShaderStageFlagBits::eFragment)
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCode<uint32_t>(meshData)
        .setPName("main")
        .setPushConstantRanges(range)
        .setSetLayouts(setLayout);
    const auto fragmentInfo = vk::ShaderCreateInfoEXT()
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
    shaders.emplace_back(nullptr);
    shaders.emplace_back(taskShader.value);
    shaders.emplace_back(meshShader.value);
    shaders.emplace_back(fragShader.value);
    return shaders;
}