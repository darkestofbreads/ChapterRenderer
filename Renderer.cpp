#define VMA_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include "Renderer.h"

Renderer::Renderer(SDL_Window* window, std::atomic<bool>* ready) {
    frameTimer = Timer();
    instance = Instance(window, ready);
    dldid = vk::detail::DispatchLoaderDynamic(instance.instance, vkGetInstanceProcAddr);

    device = Device(instance.instance);

    // Create allocator for data transfer to GPU.
    VmaVulkanFunctions vkFuncs = {};
    vkFuncs.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vkFuncs.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocInfo = {};
    allocInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocInfo.device = device.device;
    allocInfo.instance = instance.instance;
    allocInfo.physicalDevice = device.physicalDevice;
    allocInfo.pVulkanFunctions = &vkFuncs;
    vmaCreateAllocator(&allocInfo, &allocator);

    depthSubresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);

    // Swapchain and depth images.
    swapchain = Swapchain(&device.device, device.physicalDevice, instance.surface);
    for (auto& i : depthImages)
        i = CreateDepthImage();

    graphicsQueue = device.device.getQueue(device.graphicsQueueFamilyIndex, 0);

    command = Command(device);
    cmdBuffer = command.cmdBuffer;

    // Pipeline layout for push constant.
    auto perspectiveRange = vk::PushConstantRange()
        .setOffset(0)
        .setSize(sizeof(PushConstantData))
        .setStageFlags(vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment);

    // Semaphore and fence.
    auto semaphoreInfo = vk::SemaphoreCreateInfo();
    imageAquiredSemaphore = device.device.createSemaphore(semaphoreInfo);
    renderFinishedSemaphore = device.device.createSemaphore(semaphoreInfo);

    auto fenceInfo = vk::FenceCreateInfo();
    renderFinishedFence = device.device.createFence(fenceInfo);
    immediateFence = device.device.createFence(fenceInfo);

    // Camera (abstract later).
    position  = glm::vec3(0);
    direction = glm::vec3(0, 0, 1.0f);

    vertexTransform =
        glm::perspective(glm::radians(90.0f), (float)swapchain.renderExtend.width / (float)swapchain.renderExtend.height, 0.1f, 100.0f) *
        glm::lookAt(position, position + direction, glm::vec3(0, 1.0f, 0));

    // Make checkerboard texture for meshes that have missing materials.
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    uint32_t black   = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    uint32_t white   = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));

    std::array<uint32_t, 16 * 16> checkerboardData;
    for (size_t x = 0; x < 16; x++)
        for (size_t y = 0; y < 16; y++)
            checkerboardData[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;

    textures.emplace_back(CreateUploadImage(checkerboardData.data(), vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ 16, 16 }, vk::ImageUsageFlagBits::eSampled));
    textures.emplace_back(CreateUploadImage(&black, vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ 1, 1 }, vk::ImageUsageFlagBits::eSampled));
    textures.emplace_back(CreateUploadImage(&white, vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ 1, 1 }, vk::ImageUsageFlagBits::eSampled));
    materialIndexGroups.emplace_back(0, 1, 1);

    // Load test model.
    parser = fastgltf::Parser(fastgltf::Extensions::KHR_lights_punctual);

    auto dragonTrans = glm::mat4(1.0f);
    dragonTrans = glm::translate(dragonTrans, glm::vec3(5.0f, 5.0f, 2.0f));
    dragonTrans = glm::rotate<float>(dragonTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
    dragonTrans = glm::scale(dragonTrans, glm::vec3(0.1f));
    LoadGLTF("assets/stanford_dragon.glb", dragonTrans);

    auto helmetTrans = glm::mat4(1.0f);
    helmetTrans = glm::translate(helmetTrans, glm::vec3(-5.0f, 0, 0));
    helmetTrans = glm::rotate<float>(helmetTrans, glm::radians(90.0f), glm::vec3(-1, 0, 0));
    LoadGLTF("assets/DamagedHelmet.glb", helmetTrans);

    auto toyTrans = glm::mat4(1.0f);
    toyTrans = glm::translate(toyTrans, glm::vec3(-3.0f, 0, 0));
    toyTrans = glm::rotate<float>(toyTrans, glm::radians(90.0f), glm::vec3(-1, 0, 0));
    toyTrans = glm::scale(toyTrans, glm::vec3(0.005f));
    LoadGLTF("assets/ToyCar.glb", toyTrans);

    auto monkeTrans = glm::mat4(1.0f);
    monkeTrans = glm::translate(monkeTrans, glm::vec3(-2, -4, 3));
    monkeTrans = glm::rotate(monkeTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
    LoadGLTF("assets/monke.glb", monkeTrans);

    auto sponzaTrans = glm::mat4(1.0f);
    sponzaTrans = glm::translate(sponzaTrans, glm::vec3(0, 2, 0));
    sponzaTrans = glm::rotate<float>(sponzaTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
    sponzaTrans = glm::scale(sponzaTrans, glm::vec3(0.01f));
    LoadGLTF("assets/sponza.glb", sponzaTrans);

    pointLights.emplace_back(glm::vec3(20.0f,  0.0f, 0.0f), 25.0f, glm::vec3(0.0f, 0.2f, 0.5f), 10.0f);
    dirLights.emplace_back(glm::vec4(0.0f, 0.0f, -1.0f, 1), glm::vec4(0.35f, 0.0f, 0.1f, 1));
    dirLights.emplace_back(glm::vec4(1.0f, 0.0f, 1.0f, 1), glm::vec4(0.0f, 0.0005f, 0.0f, 1));
    spotLights.emplace_back(glm::vec3(-9.0f, -1.0f, 2.0f), 10.0f, glm::vec4(1.0f, 0.0f, -1.0f, 1), glm::vec3(1), 0.0f, 0.95f, 0.96f);

    // Upload geometry and material indices.
    if (indices.size() > 0 && vertices.size() > 0)
        meshBuffer = UploadMesh(indices, vertices);
    // Upload lights.
    if (materialIndexGroups.size() > 0)
        materialBufferAddress   = UploadData<MaterialIndexGroup>(materialIndexGroups);
    if (pointLights.size() > 0)
        pointLightBufferAddress = UploadData<PointLight>(pointLights);
    if (spotLights.size() > 0)
        spotLightBufferAddress  = UploadData<SpotLight>(spotLights);
    if (dirLights.size() > 0)
        dirLightBufferAddress   = UploadData<DirLight>(dirLights);

    // Texture samplers.
    auto nearestSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest);
    auto linearSamplerInfo  = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear);
    auto nearLinSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest);
    auto linNearSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear);

    nearestSampler = device.device.createSampler(nearestSamplerInfo);
    linearSampler  = device.device.createSampler(linearSamplerInfo);

    // Set bindings for the push descriptor (textures are on set = 0, binding = 0).
    auto layoutBinding = vk::DescriptorSetLayoutBinding()
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(textures.size())
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    auto descriptorLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
        .setBindings(layoutBinding)
        .setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor);
    imageDescLayout = device.device.createDescriptorSetLayout(descriptorLayoutInfo);

    // Shader object.
    shaders = MakeMeshShaderObjects(device.device, "shaders/triangle.mesh.spv", "shaders/fragment.frag.spv", dldid, perspectiveRange, imageDescLayout);

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
        .setPushConstantRanges(perspectiveRange)
        .setSetLayouts(imageDescLayout);
    pipelineLayout = device.device.createPipelineLayout(pipelineLayoutInfo);

    // Setup UI.
    InitImGui(window);
}

void Renderer::Draw() {
    double frameTime = frameTimer.GetMilliseconds();
    frameTimer.Reset();

    // Aquire next image.
    auto imageNext = device.device.acquireNextImageKHR(swapchain.Get(), UINT64_MAX, imageAquiredSemaphore, nullptr);
    auto imageIndex = imageNext.value;
    auto imageResult = imageNext.result;

    if (imageResult == vk::Result::eSuboptimalKHR || imageResult == vk::Result::eErrorOutOfDateKHR) {
        swapchain.Recreate(instance.pWindow);
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    std::string positionStr = "X: " + std::to_string(position.x) + " Y: " + std::to_string(position.y) + " Z: " + std::to_string(position.z) + "\n";
    std::string frameTimeStr = std::to_string(frameTime) + " ms | " + std::to_string(1000 / frameTime) + " fps\n";
    ImGui::Text(positionStr.c_str());
    ImGui::Text(frameTimeStr.c_str());

    BeginRendering(imageIndex);

    BuildGlobalTransform();

    glm::vec4 lightsCount(0);
    lightsCount.x = pointLights.size();
    lightsCount.y = spotLights.size();
    lightsCount.z = dirLights.size();

    PushConstantData pushConstant{
        vertexTransform,
        worldTransform,
        lightsCount,
        meshBuffer.vertexBufferAddress,
        meshBuffer.indexBufferAddress,
        materialBufferAddress,
        pointLightBufferAddress,
        spotLightBufferAddress,
        dirLightBufferAddress
    };

    std::vector<vk::DescriptorImageInfo> imageDescriptors;
    imageDescriptors.reserve(textures.size());
    for (size_t i = 0; i < textures.size(); i++) {
        auto imageDescriptor = vk::DescriptorImageInfo()
            .setSampler(nearestSampler)
            .setImageLayout(vk::ImageLayout::eAttachmentOptimal)
            .setImageView(textures[i].view);

        imageDescriptors.emplace_back(imageDescriptor);
    }

    auto descWrite = vk::WriteDescriptorSet()
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDstBinding(0)
        .setDescriptorCount(1)
        .setImageInfo(imageDescriptors);
    std::array<vk::WriteDescriptorSet, 1> descWrites{
        descWrite
    };

    auto pushDescInfo = vk::PushDescriptorSetInfo()
        .setLayout(pipelineLayout)
        .setDescriptorWrites(descWrites)
        .setDescriptorWriteCount(descWrites.size())
        .setSet(0)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    cmdBuffer.pushDescriptorSet2(pushDescInfo);
    cmdBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstantData), &pushConstant);

    // Draw image.
    cmdBuffer.bindShadersEXT(meshStages, shaders, dldid);
    cmdBuffer.drawMeshTasksEXT(static_cast<uint32_t>(indices.size()), 1, 1, dldid);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(command.cmdBuffer));

    // End the rendering process and transition our image to be presentable.
    cmdBuffer.endRendering();
    command.TransitionImage(swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eNone);
    command.TransitionImage(depthImages[imageIndex].image, depthSubresourceRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
     vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eNone);
    cmdBuffer.end();


    // Submit and present image.
    SubmitDraw();
    Present(imageIndex);
}

// Camera related functions.
void Renderer::Move(float forward, float sideward) {
    position += forward * direction;
    position -= glm::normalize(glm::cross(direction, glm::vec3(0, 1, 0))) * sideward;
}
void Renderer::BuildGlobalTransform() {
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction = glm::normalize(direction);

    worldTransform = glm::lookAt(position, position + direction, glm::vec3(0, 1.0f, 0));
    vertexTransform = {
        glm::perspective(glm::radians(90.0f), (float)swapchain.renderExtend.width / (float)swapchain.renderExtend.height, 0.1f, 4000.0f) *
        worldTransform
    };
}

void Renderer::BeginRendering(uint32_t imageIndex) {
    auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuffer.begin(beginInfo);

    command.TransitionImage(swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eColorAttachmentWrite);
    command.TransitionImage(depthImages[imageIndex].image, depthSubresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AccessFlagBits2::eNone,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);

    command.SetDynamicStates(dldid);

    auto viewport = vk::Viewport()
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f)
        .setHeight(swapchain.renderExtend.height)
        .setWidth(swapchain.renderExtend.width)
        .setX(0)
        .setY(0);
    cmdBuffer.setViewportWithCount(viewport);

    auto scissor = vk::Rect2D()
        .setExtent(swapchain.renderExtend)
        .setOffset({ 0 ,0 });
    cmdBuffer.setScissorWithCount(scissor);

    auto colorAttachment = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue({ 0.1f, 0.1f, 0.3f, 1.0f }))
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setImageView(swapchain.imageViews[imageIndex])
        .setResolveMode(vk::ResolveModeFlagBits::eNone);

    cmdBuffer.setDepthTestEnable(vk::True);
    cmdBuffer.setDepthWriteEnable(vk::True);
    cmdBuffer.setDepthCompareOp(vk::CompareOp::eLessOrEqual);

    auto depthAttachment = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setClearValue(vk::ClearDepthStencilValue(1.0f, 0))
        .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
        .setImageView(depthImages[imageIndex].view)
        .setResolveMode(vk::ResolveModeFlagBits::eNone)
        .setResolveImageLayout(vk::ImageLayout::eUndefined);

    auto renderArea = vk::Rect2D()
        .setExtent(swapchain.renderExtend);

    vk::RenderingInfo renderInfo(vk::RenderingFlags(), renderArea, 1, 0, colorAttachment, &depthAttachment);
    cmdBuffer.beginRendering(renderInfo);
}
void Renderer::SubmitDraw() {
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo = vk::SubmitInfo()
        .setCommandBuffers(cmdBuffer)
        .setWaitSemaphores(imageAquiredSemaphore)
        .setSignalSemaphores(renderFinishedSemaphore)
        .setWaitDstStageMask(waitStage);
    graphicsQueue.submit(submitInfo, renderFinishedFence);
}
void Renderer::SubmitImmediate(std::function<void()>& func) {
    device.device.resetFences(immediateFence);

    vk::CommandBufferBeginInfo beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuffer.begin(beginInfo);

    func();

    cmdBuffer.end();

    vk::SubmitInfo submitInfo = vk::SubmitInfo()
        .setCommandBuffers(cmdBuffer);
    graphicsQueue.submit(submitInfo, immediateFence);
    device.device.waitForFences(immediateFence, false, UINT64_MAX);
}
void Renderer::Present(uint32_t imageIndex) {
    vk::PresentInfoKHR info = vk::PresentInfoKHR()
        .setSwapchains(swapchain.swapchain)
        .setImageIndices(imageIndex)
        .setWaitSemaphores(renderFinishedSemaphore);

    try {
        graphicsQueue.presentKHR(info);
    }
    catch (std::exception e) {
        device.device.waitForFences(renderFinishedFence, false, UINT64_MAX);
        device.device.resetFences(renderFinishedFence);
        device.device.resetCommandPool(command.cmdPool);
        swapchain.Recreate(instance.pWindow);
        return;
    }
    
    device.device.waitForFences(renderFinishedFence, false, UINT64_MAX);
    device.device.resetFences(renderFinishedFence);
    device.device.resetCommandPool(command.cmdPool);
}

void Renderer::InitImGui(SDL_Window* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.FontGlobalScale = 3.0f;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo imGuiInitInfo = {};
    imGuiInitInfo.UseDynamicRendering = true;
    imGuiInitInfo.ApiVersion = VK_API_VERSION_1_4;
    imGuiInitInfo.Device = device.device;
    imGuiInitInfo.ImageCount = swapchain.images.size();
    imGuiInitInfo.Instance = instance.instance;
    imGuiInitInfo.MinImageCount = swapchain.images.size();
    imGuiInitInfo.PhysicalDevice = device.physicalDevice;
    imGuiInitInfo.Queue = static_cast<VkQueue>(graphicsQueue);
    imGuiInitInfo.QueueFamily = device.graphicsQueueFamilyIndex;
    imGuiInitInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 1;
    imGuiInitInfo.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    imGuiInitInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    imGuiInitInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = reinterpret_cast<VkFormat*>(&swapchain.renderFormat);
    imGuiInitInfo.MinAllocationSize = 1024 * 1024;
    ImGui_ImplVulkan_Init(&imGuiInitInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
    clearColorUI = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
}

// Read 3D model
template<typename T>
std::vector<T> ReadAttribute(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, std::string_view Attribute) {
    const auto& iterator = primitive.findAttribute(Attribute);
    assert(iterator != nullptr);

    const auto& acr = asset.accessors[iterator->accessorIndex];
    const auto& bufferView = asset.bufferViews[acr.bufferViewIndex.value()];

    const auto& buffer = asset.buffers[bufferView.bufferIndex];
    const auto& data = get<fastgltf::sources::Array>(buffer.data);

    std::vector<T> out(acr.count);
    std::memcpy(out.data(), data.bytes.data() + bufferView.byteOffset + acr.byteOffset, acr.count * sizeof(T));
    return out;
}
uint32_t Renderer::ParseGLTFImage(const fastgltf::TextureInfo& imageInfo, const fastgltf::Asset& asset, std::vector<AllocatedImage>& textures) {
    const auto& texture          = asset.textures[imageInfo.textureIndex];
    const auto& image            = asset.images[texture.imageIndex.value()];
    const auto& sourceBufferView = get<fastgltf::sources::BufferView>(image.data);
    
    const auto& imageBufferView  = asset.bufferViews[sourceBufferView.bufferViewIndex];
    const auto& imageBuffer      = asset.buffers[imageBufferView.bufferIndex];
    const auto& imageData        = get<fastgltf::sources::Array>(imageBuffer.data);

    std::vector<unsigned char> imageChars(imageBufferView.byteLength);
    std::memcpy(imageChars.data(), imageData.bytes.data() + imageBufferView.byteOffset, imageBufferView.byteLength);
    unsigned char* pixels;
    if (sourceBufferView.mimeType == fastgltf::MimeType::JPEG || sourceBufferView.mimeType == fastgltf::MimeType::PNG) {
        int width, height, comp;
        pixels = stbi_load_from_memory(imageChars.data(), imageBufferView.byteLength, &width, &height, &comp, STBI_rgb_alpha);
        textures.emplace_back(CreateUploadImage(pixels, vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }, vk::ImageUsageFlagBits::eSampled));
        stbi_image_free(pixels);
    }
    else if (sourceBufferView.mimeType == fastgltf::MimeType::KTX2) {
        //ktxTexture* textureKTX;
        //const auto& result = ktxTexture_CreateFromMemory(imageChars.data(), imageChars.size(), KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT, &textureKTX);
        //if (!result)
        //    return 0;
        //pixels = ktxTexture_GetData(textureKTX);
        //textures.emplace_back(CreateUploadImage(pixels, vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ textureKTX->baseWidth, textureKTX->baseHeight }, vk::ImageUsageFlagBits::eSampled));
        //ktxTexture_Destroy(textureKTX);
        return 0;
    }
    else
        return 0;
    return textures.size() - 1;
}
void Renderer::LoadGLTF(std::filesystem::path path, glm::mat4 transform) {

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (auto error = data.error(); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
    auto gltf = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadGLBBuffers);
    if (auto error = gltf.error(); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
    auto asset = std::move(gltf.get());

#if defined(_DEBUG)
    if (auto error = fastgltf::validate(asset); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
#endif

    auto normalTransform = glm::mat3(glm::transpose(glm::inverse(transform)));

    for (const auto& node : asset.nodes) {
        // Load light.
        if (node.lightIndex.has_value()) {
            const auto& light = asset.lights[node.lightIndex.value()];
            const auto& nodeData = get<fastgltf::TRS>(node.transform);
            switch (light.type) {
            case fastgltf::LightType::Point:
                PointLight pl;
                pl.color    = glm::vec3(light.color.x(), light.color.y(), light.color.z());
                pl.Position = glm::vec3(transform * glm::vec4(nodeData.translation.x(), nodeData.translation.y(), nodeData.translation.z(), 1));
                pl.falloff  = 0;

                pl.radius = 100;
                if (light.range.has_value())
                    pl.radius = light.range.value();
                pointLights.emplace_back(pl);
                break;
            case fastgltf::LightType::Spot:
                SpotLight sl;
                sl.color       = glm::vec3(light.color.x(), light.color.y(), light.color.z());
                sl.pos         = glm::vec3(transform * glm::vec4(nodeData.translation.x(), nodeData.translation.y(), nodeData.translation.z(), 1));
                //sl.lightDir = glm::fquat(nodeData.rotation.w(), nodeData.rotation.x(), nodeData.rotation.y(), nodeData.rotation.z());
                sl.falloff     = 0;
                sl.cutoff      = glm::radians(light.outerConeAngle.value());
                sl.innerCutoff = glm::radians(light.innerConeAngle.value());
                
                sl.radius = 100;
                if (light.range.has_value())
                    sl.radius = light.range.value();
                //spotLights.emplace_back(sl);
                break;
            case fastgltf::LightType::Directional:
                DirLight dl;
                dl.color = glm::vec4(light.color.x(), light.color.y(), light.color.z(), 1);
                //dl.lightDir = glm::fquat(nodeData.rotation.w(), nodeData.rotation.x(), nodeData.rotation.y(), nodeData.rotation.z());
                //dirLights.emplace_back(dl);
                break;
            }
        }
    }
    // Load materials.
    std::vector<uint32_t> materialIDs;
    std::vector<MaterialIndexGroup> matIndexGroups;
    for (const auto& material : asset.materials) {
        MaterialIndexGroup materialIndices;
        const auto& pbrData = material.pbrData;

        if (pbrData.baseColorTexture.has_value())
            materialIndices.diffuse = ParseGLTFImage(pbrData.baseColorTexture.value(), asset, textures);
        else
            materialIndices.diffuse = 0;

        if (pbrData.metallicRoughnessTexture.has_value())
            materialIndices.metallicRoughness = ParseGLTFImage(pbrData.metallicRoughnessTexture.value(), asset, textures);
        else
            materialIndices.metallicRoughness = 1;

        if (material.emissiveTexture.has_value())
            materialIndices.emissive = ParseGLTFImage(material.emissiveTexture.value(), asset, textures);
        else
            materialIndices.emissive = 1;

        materialIDs.emplace_back(materialIndexGroups.size());
        materialIndexGroups.emplace_back(materialIndices);
    }
    // Load meshes.
    for (const auto& mesh : asset.meshes) {
        for (const auto& primitive : mesh.primitives) {
            size_t prevVertexSize = vertices.size();
            size_t prevIndexSize  = indices.size();

            auto positions        = ReadAttribute<glm::vec3>(asset, primitive, "POSITION");
            auto normals          = ReadAttribute<glm::vec3>(asset, primitive, "NORMAL");
            const auto& texCoords = ReadAttribute<glm::vec2>(asset, primitive, "TEXCOORD_0");

            for (size_t t = 0; t < positions.size(); t++) {
                auto pos = transform * glm::vec4(positions[t], 1);
                positions[t] = pos.xyz;
                normals[t]   = normalTransform * normals[t];
            }

            // Add vertices to pool.
            vertices.reserve(vertices.size() + positions.size());
            for (size_t i = 0; i < positions.size(); i++) {
                vertices.emplace_back(positions[i], texCoords[i].x, normals[i], texCoords[i].y);
            }

            // Determine material.
            uint32_t virtualMaterialIndex = 0;
            if (primitive.materialIndex.has_value()) {
                virtualMaterialIndex = materialIDs[primitive.materialIndex.value()];
            }

            // Load indices.
            auto& indIt = primitive.indicesAccessor;
            assert(indIt.has_value());

            const auto& indAcr        = asset.accessors[indIt.value()];
            const auto& indBufferView = asset.bufferViews[indAcr.bufferViewIndex.value()];
            const auto& indBuffer     = asset.buffers[indBufferView.bufferIndex];
            const auto& indData       = get<fastgltf::sources::Array>(indBuffer.data);

            size_t triangleCount      = indAcr.count / 3;
            indices.resize(triangleCount + indices.size());

            size_t count = 0;
            // If possible, the GLTF will use uint16 to reduce file size.
            if (indAcr.componentType == fastgltf::ComponentType::UnsignedShort) {
                std::vector<uint16_t> rawIndices(indAcr.count);
                std::memcpy(rawIndices.data(), indData.bytes.data() + indAcr.byteOffset + indBufferView.byteOffset, sizeof(uint16_t) * rawIndices.size());

                for (size_t i = 0; i < triangleCount; i++) {
                    indices[prevIndexSize + i] =
                        glm::uvec4(prevVertexSize + static_cast<uint32_t>(rawIndices[count]), prevVertexSize + static_cast<uint32_t>(rawIndices[count + 1]),
                                   prevVertexSize + static_cast<uint32_t>(rawIndices[count + 2]), virtualMaterialIndex);
                    count += 3;
                }
            }
            else {
                std::vector<uint32_t> rawIndices(indAcr.count);
                std::memcpy(rawIndices.data(), indData.bytes.data() + indAcr.byteOffset + indBufferView.byteOffset, sizeof(uint32_t) * rawIndices.size());

                for (size_t i = 0; i < triangleCount; i++) {
                    indices[prevIndexSize + i] = glm::uvec4(prevVertexSize + rawIndices[count], prevVertexSize + rawIndices[count + 1],
                                                            prevVertexSize + rawIndices[count + 2], virtualMaterialIndex);
                    count += 3;
                }
            }
        }
    }
}

template<typename T>
vk::DeviceAddress Renderer::UploadData(std::span<T> data) {
    const auto size = sizeof(T) * data.size();

    auto buffer = CreateBuffer(size, vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);
    auto addressInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(buffer.buffer);

    AllocatedBuffer stageBuffer = CreateBuffer(size,
        vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    auto byteData = static_cast<std::byte*>(stageBuffer.alloc->GetMappedData());
    std::memcpy(byteData, data.data(), size);

    std::function<void()> func = [&]() {
        auto region = vk::BufferCopy()
            .setSize(size);
        cmdBuffer.copyBuffer(stageBuffer.buffer, buffer.buffer, region);
        };
    SubmitImmediate(func);
    device.device.resetCommandPool(command.cmdPool);
    vmaDestroyBuffer(allocator, stageBuffer.buffer, stageBuffer.alloc);

    return device.device.getBufferAddress(addressInfo);
}
AllocatedBuffer Renderer::CreateBuffer(size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage) {
    VkBufferCreateInfo bufferInfo = vk::BufferCreateInfo()
        .setSize(allocSize)
        .setUsage(usage);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer allocBuffer;
    VkBuffer buffer;
    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocBuffer.alloc, &allocBuffer.info);

    allocBuffer.buffer = vk::Buffer(buffer);
    return allocBuffer;
}
GPUMeshBuffer Renderer::UploadMesh(std::span<glm::uvec4> indices, std::span<Vertex> vertices) {
    // GPU only buffers.
    const size_t vertSize = vertices.size() * sizeof(Vertex);
    const size_t indiSize =  indices.size() * sizeof(glm::uvec4);

    auto vertBuffer = CreateBuffer(vertSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);
    auto indexBuffer = CreateBuffer(vertSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);

    auto vertInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(vertBuffer.buffer);
    auto indexInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(indexBuffer.buffer);

    GPUMeshBuffer meshbuffer{
        indexBuffer,
        vertBuffer,
        device.device.getBufferAddress(vertInfo),
        device.device.getBufferAddress(indexInfo)
    };

    // Temporary CPU buffer for sending data.
    AllocatedBuffer stageBuffer = CreateBuffer(vertSize + indiSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    auto data = static_cast<std::byte*>(stageBuffer.alloc->GetMappedData());

    std::memcpy(data, vertices.data(), vertSize);
    std::memcpy(data + vertSize, indices.data(), indiSize);

    std::function<void()> func = [&]() {
        auto vertRegion = vk::BufferCopy()
            .setSize(vertSize);
        cmdBuffer.copyBuffer(stageBuffer.buffer, meshbuffer.vertexBuffer.buffer, vertRegion);

        auto indexRegion = vk::BufferCopy()
            .setSrcOffset(vertSize)
            .setSize(indiSize);
        cmdBuffer.copyBuffer(stageBuffer.buffer, meshbuffer.indexBuffer.buffer, indexRegion);
    };
    SubmitImmediate(func);

    device.device.resetCommandPool(command.cmdPool);
    vmaDestroyBuffer(allocator, stageBuffer.buffer, stageBuffer.alloc);

    return meshbuffer;
}

AllocatedImage Renderer::CreateDepthImage() {
    // Get supported depth format.
    std::array<vk::Format, 3> depthFormats = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint
    };
    vk::Format depthFormat;
    for (auto& f : depthFormats) {
        auto properties = device.physicalDevice.getFormatProperties(f);
        if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            depthFormat = f;
            break;
        }
    }
    return CreateImage(vk::Format::eD24UnormS8Uint, swapchain.renderExtend, vk::ImageUsageFlagBits::eDepthStencilAttachment, depthSubresourceRange);
}
AllocatedImage Renderer::CreateImage(vk::Format format, vk::Extent2D extend, vk::ImageUsageFlags usage, vk::ImageSubresourceRange subresource, bool makeMipmaps) {
    uint32_t mipLevelCount = 1;
    if (makeMipmaps)
        mipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(extend.height, extend.width)))) + 1;

    auto imageInfo = vk::ImageCreateInfo()
        .setArrayLayers(1)
        .setExtent(vk::Extent3D(extend, 1))
        .setFlags(vk::ImageCreateFlags())
        .setFormat(format)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setMipLevels(mipLevelCount)
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setImageType(vk::ImageType::e2D)
        .setTiling(vk::ImageTiling::eOptimal)
        .setQueueFamilyIndices(0);

    VmaAllocationCreateInfo imageAllocCreateInfo = {};
    imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    imageAllocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    vk::Image image;
    VmaAllocation alloc;
    auto result = vmaCreateImage(allocator, reinterpret_cast<VkImageCreateInfo*>(&imageInfo), &imageAllocCreateInfo, reinterpret_cast<VkImage*>(&image), &alloc, nullptr);
    
    vk::ImageView imageView;
    imageView = CreateImageView(image, format, subresource);
    
    return {image, imageView, alloc};
}
AllocatedImage Renderer::CreateUploadImage(void* data, vk::Format format, vk::Extent2D extend, vk::ImageUsageFlags usage, bool makeMipmaps) {

    size_t size = extend.height * extend.width * 4;
    auto upload = CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

    auto subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);
    
    std::memcpy(upload.info.pMappedData, data, size);
    auto image = CreateImage(format, extend, usage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, subresourceRange, makeMipmaps);
    
    std::function<void()> func = [&]() {
        command.TransitionImage(image.image, swapchain.subresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eTransferWrite);

        auto imageSubresource = vk::ImageSubresourceLayers()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(1);
        auto imageCopy = vk::BufferImageCopy()
            .setBufferOffset(0)
            .setBufferImageHeight(0)
            .setBufferRowLength(0)
            .setImageExtent(vk::Extent3D(extend, 1))
            .setImageSubresource(imageSubresource);

        command.cmdBuffer.copyBufferToImage(upload.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, imageCopy);
        command.TransitionImage(image.image, swapchain.subresourceRange, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderSampledRead);
    };
    SubmitImmediate(func);
    device.device.resetCommandPool(command.cmdPool);

    return image;
}

vk::ImageView Renderer::CreateImageView(const vk::Image& image, const vk::Format& format, const vk::ImageSubresourceRange& subresource) {
    auto identity = vk::ComponentSwizzle::eIdentity;
    auto compMapping = vk::ComponentMapping()
        .setA(identity)
        .setB(identity)
        .setG(identity)
        .setR(identity);

    auto imageViewInfo = vk::ImageViewCreateInfo()
        .setComponents(compMapping)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(format)
        .setImage(image)
        .setSubresourceRange(subresource);
    return device.device.createImageView(imageViewInfo);
}