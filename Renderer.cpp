#define VMA_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include "Renderer.h"

Renderer::Renderer(SDL_Window* window, std::atomic<bool>* ready) {
    InitMainObjects(window, ready);
    CreateFencesAndSemaphores();

    CreateSamplers_Init();
    CreateDebugTextures();

    vertices.resize(6);

    LoadModels_Init();
    SpawnLights_Init();
    UploadAll_Init();

    CreateDescSets_Init();
    CreatePipeline();

    // Setup UI.
    InitImGui(window);
}

void Renderer::Draw() {
    uint32_t imageIndex;
    if (!AquireImageIndex(imageIndex)) return;

    double frameTime = frameTimer.GetMilliseconds();
    if (drawUI) ImGui_Draw(frameTime);
    frameTimer.Reset();

    vk::RenderingAttachmentInfo colorAttachment;
    vk::RenderingAttachmentInfo depthAttachment;
    vk::Rect2D renderArea;
    Begin(imageIndex, colorAttachment, depthAttachment, renderArea);
    PushConstant_Draw();

    // Fill screen tile frustum buffer if FOV or resolution changes.
    if (firstTime || requestNewSwapchain) {
        uint32_t lightCullX = (swapchain.renderExtend.width + (swapchain.renderExtend.width % 16)) / 16;
        uint32_t lightCullY = (swapchain.renderExtend.height + (swapchain.renderExtend.height % 16)) / 16;
        graphCompCmdBuffers[currentFrame].bindShadersEXT(vk::ShaderStageFlagBits::eCompute, screenTileFrustumsShader, dldid);
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSets, nullptr);
        graphCompCmdBuffers[currentFrame].dispatch(lightCullX, lightCullY, 1);

        const auto tileFrustumsBarrier = vk::BufferMemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderStorageRead)
            .setBuffer(tileFrustumBuffer.buffer)
            .setSize(tileFrustumsSize);

        const auto tileFrustumsDependencyInfo = vk::DependencyInfo()
            .setBufferMemoryBarriers(tileFrustumsBarrier);

        graphCompCmdBuffers[currentFrame].pipelineBarrier2(tileFrustumsDependencyInfo);
    }

    // Depth pre-pass.
    graphCompCmdBuffers[currentFrame].setDepthTestEnable(vk::True);
    graphCompCmdBuffers[currentFrame].setDepthWriteEnable(vk::True);
    graphCompCmdBuffers[currentFrame].setDepthCompareOp(vk::CompareOp::eLess);
    vk::RenderingInfo renderInfo(vk::RenderingFlagBits::eSuspending, renderArea, 1, 0, colorAttachment, &depthAttachment);

    // Depth prepass.
    graphCompCmdBuffers[currentFrame].beginRendering(renderInfo);
    {
        graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, depthprepassShaders, dldid);
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets, nullptr);
        graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);

        graphCompCmdBuffers[currentFrame].setDepthWriteEnable(vk::False);
        graphCompCmdBuffers[currentFrame].setDepthCompareOp(vk::CompareOp::eEqual);
    }
    graphCompCmdBuffers[currentFrame].endRendering();

    // Light culling.
    if (doLightCulling) {
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSets, nullptr);

        const auto depthToComputeBarrier = vk::ImageMemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
            .setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
            .setOldLayout(vk::ImageLayout::eAttachmentOptimal)
            .setNewLayout(vk::ImageLayout::eReadOnlyOptimal)
            .setImage(depthImages[0].image)
            .setSubresourceRange(depthSubresourceRange);
        const auto depthToComputeDependencyInfo = vk::DependencyInfo()
            .setImageMemoryBarriers(depthToComputeBarrier);
        graphCompCmdBuffers[currentFrame].pipelineBarrier2(depthToComputeDependencyInfo);
    
        // Bind depth buffer.
        auto depthDescriptor = vk::DescriptorImageInfo()
            .setSampler(nearestSampler)
            .setImageLayout(vk::ImageLayout::eReadOnlyOptimal)
            .setImageView(depthImages[0].view);
    
        auto descWrite = vk::WriteDescriptorSet()
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDstSet(descriptorSets[0])
            .setDstBinding(1)
            .setDescriptorCount(1)
            .setImageInfo(depthDescriptor);
        device.device.updateDescriptorSets(descWrite, nullptr);
    
        uint32_t lightCullX = (swapchain.renderExtend.width  + (swapchain.renderExtend.width  % 16)) / 16;
        uint32_t lightCullY = (swapchain.renderExtend.height + (swapchain.renderExtend.height % 16)) / 16;
        graphCompCmdBuffers[currentFrame].bindShadersEXT(vk::ShaderStageFlagBits::eCompute, lightCullingShader, dldid);
        graphCompCmdBuffers[currentFrame].dispatch(lightCullX, lightCullY, 1);
        
        const auto lightIndicesBarrier = vk::BufferMemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderStorageRead)
            .setBuffer(lightIndicesBuffer.buffer)
            .setSize(lightIndicesSize);

        const auto depthToGraphicsBarrier = vk::ImageMemoryBarrier2()
            .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
            .setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
            .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setSrcAccessMask(vk::AccessFlagBits2::eShaderRead)
            .setOldLayout(vk::ImageLayout::eReadOnlyOptimal)
            .setNewLayout(vk::ImageLayout::eAttachmentOptimal)
            .setImage(depthImages[0].image)
            .setSubresourceRange(depthSubresourceRange);

        const auto lightCullingDependencyInfo = vk::DependencyInfo()
            .setImageMemoryBarriers(depthToGraphicsBarrier)
            .setBufferMemoryBarriers(lightIndicesBarrier);
        graphCompCmdBuffers[currentFrame].pipelineBarrier2(lightCullingDependencyInfo);
    }

    // Forward shading and specular.
    renderInfo.setFlags(vk::RenderingFlagBits::eResuming);
    graphCompCmdBuffers[currentFrame].beginRendering(renderInfo);
    if (doLightCulling) {
        graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, forwardPlusShaders, dldid);
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets, nullptr);
        graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
    }
    else {
        graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, forwardShaders, dldid);
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets, nullptr);
        graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
    }

    if (drawUI) {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(graphCompCmdBuffers[currentFrame]));
    }

    graphCompCmdBuffers[currentFrame].endRendering();

    SubmitAndPresent(imageIndex);
}

// Camera related functions.
// Gribb Hartmann method of extracting the frustum planes from the view-projection matrix
std::array<glm::vec4, 6> ExtractFrustum(glm::mat4 mat) {
    glm::vec4 near;
    near[0] = mat[0].w + mat[0].z;
    near[1] = mat[1].w + mat[1].z;
    near[2] = mat[2].w + mat[2].z;
    near[3] = mat[3].w + mat[3].z;

    glm::vec4 far;
    far[0] = mat[0].w - mat[0].z;
    far[1] = mat[1].w - mat[1].z;
    far[2] = mat[2].w - mat[2].z;
    far[3] = mat[3].w - mat[3].z;

    glm::vec4 right;
    right[0] = mat[0].w - mat[0].x;
    right[1] = mat[1].w - mat[1].x;
    right[2] = mat[2].w - mat[2].x;
    right[3] = mat[3].w - mat[3].x;

    glm::vec4 left;
    left[0] = mat[0].w + mat[0].x;
    left[1] = mat[1].w + mat[1].x;
    left[2] = mat[2].w + mat[2].x;
    left[3] = mat[3].w + mat[3].x;

    glm::vec4 top;
    top[0] = mat[0].w - mat[0].y;
    top[1] = mat[1].w - mat[1].y;
    top[2] = mat[2].w - mat[2].y;
    top[3] = mat[3].w - mat[3].y;

    glm::vec4 bottom;
    bottom[0] = mat[0].w + mat[0].y;
    bottom[1] = mat[1].w + mat[1].y;
    bottom[2] = mat[2].w + mat[2].y;
    bottom[3] = mat[3].w + mat[3].y;

    std::array<glm::vec4, 6> planes = { near, far, right, left, top, bottom };
    for (size_t i = 0; i < planes.size(); i++) {
        const auto lenght = std::sqrtf(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
        planes[i] /= lenght;
    }

    return planes;
}
void Renderer::Move(float forward, float sideward) {
    position += forward * direction;
    position -= glm::normalize(glm::cross(direction, glm::vec3(0, 1, 0))) * sideward;
}
void Renderer::Teleport(glm::vec3 pos, glm::vec3 direction) {
    position = pos;
    yaw = -90;
    pitch = 0;
}
void Renderer::BuildGlobalTransform() {
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction = glm::normalize(direction);

    const float ratio = (float)swapchain.renderExtend.width / (float)swapchain.renderExtend.height;
    const float near  = 0.1f;
    const float far   = 4000.0f;
    const float fovY  = 90.0f;
    const auto up     = glm::vec3(0, 1.0f, 0);

    view = glm::lookAt(position, position + direction, up);
    proj = glm::perspective(glm::radians(fovY), ratio, near, far);
    projViewTransform = {
        proj * view
    };
    const auto frustum = ExtractFrustum(projViewTransform);
    vertices[0] = { frustum[0].xyz, frustum[0].w, glm::vec3(0), 0 };
    vertices[1] = { frustum[1].xyz, frustum[1].w, glm::vec3(0), 0 };
    vertices[2] = { frustum[2].xyz, frustum[2].w, glm::vec3(0), 0 };
    vertices[3] = { frustum[3].xyz, frustum[3].w, glm::vec3(0), 0 };
    vertices[4] = { frustum[4].xyz, frustum[4].w, glm::vec3(0), 0 };
    vertices[5] = { frustum[5].xyz, frustum[5].w, glm::vec3(0), 0 };
}

bool Renderer::AquireImageIndex(uint32_t& index) {
    const auto imageNext   = device.device.acquireNextImageKHR(swapchain.Get(), UINT64_MAX, imageAquiredSemaphores[currentFrame], nullptr);
    const auto imageResult = imageNext.result;
    index = imageNext.value;
    if (imageResult == vk::Result::eSuboptimalKHR || imageResult == vk::Result::eErrorOutOfDateKHR) {
        swapchain.Recreate(instance.pWindow, doVsync);
        requestNewSwapchain = false;
        return false;
    }
    return true;
}
void Renderer::Begin(const uint32_t imageIndex, vk::RenderingAttachmentInfo& colorAttachment, vk::RenderingAttachmentInfo& depthAttachment, vk::Rect2D& renderArea) {
    depthAttachment = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearDepthStencilValue(1.0f, 0))
        .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
        .setImageView(depthImages[0].view)
        .setResolveMode(vk::ResolveModeFlagBits::eNone)
        .setResolveImageLayout(vk::ImageLayout::eUndefined);
    colorAttachment = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue({ 0.1f, 0.1f, 0.3f, 1.0f }))
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setImageView(swapchain.imageViews[imageIndex])
        .setResolveMode(vk::ResolveModeFlagBits::eNone);

    auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    graphCompCmdBuffers[currentFrame].begin(beginInfo);

    TransitionImage(graphCompCmdBuffers[currentFrame], swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, vk::AccessFlagBits2::eNone,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    TransitionImage(graphCompCmdBuffers[currentFrame], depthImages[0].image, depthSubresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AccessFlagBits2::eNone,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    SetDynamicStates(graphCompCmdBuffers[currentFrame], dldid);

    auto viewport = vk::Viewport()
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f)
        .setHeight(swapchain.renderExtend.height)
        .setWidth(swapchain.renderExtend.width)
        .setX(0)
        .setY(0);
    graphCompCmdBuffers[currentFrame].setViewportWithCount(viewport);
    auto scissor = vk::Rect2D()
        .setExtent(swapchain.renderExtend)
        .setOffset({ 0 ,0 });
    graphCompCmdBuffers[currentFrame].setScissorWithCount(scissor);

    renderArea = vk::Rect2D()
        .setExtent(swapchain.renderExtend);

    BuildGlobalTransform();
    if (!freezeFrustum) {
        auto frustumSpan = std::span<Vertex>(vertices).subspan(0, 6);
        UpdateBuffer(meshBuffer, frustumSpan, stageBuffers[currentFrame]);

        auto barrier = vk::MemoryBarrier2()
            .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
            .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
            .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
            .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite);
        auto depInfo = vk::DependencyInfo()
            .setMemoryBarriers(barrier);
        graphCompCmdBuffers[currentFrame].pipelineBarrier2(depInfo);
    }
}
void Renderer::SubmitImmediate(const std::function<void()>& func) {
    device.device.resetFences(immediateFence);

    vk::CommandBufferBeginInfo beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    graphCompCmdBuffers[0].begin(beginInfo);
    graphCompCmdBuffers[1].begin(beginInfo);

    func();

    graphCompCmdBuffers[0].end();
    graphCompCmdBuffers[1].end();

    vk::SubmitInfo graphicsSubmitInfo = vk::SubmitInfo()
        .setCommandBuffers(graphCompCmdBuffers);
    graphicsComputeQueue.submit(graphicsSubmitInfo, immediateFence);
    device.device.waitForFences(immediateFence, false, UINT64_MAX);
}
void Renderer::SubmitAndPresent(uint32_t imageIndex) {
    TransitionImage(graphCompCmdBuffers[currentFrame], swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eNone);
    TransitionImage(graphCompCmdBuffers[currentFrame], depthImages[0].image, depthSubresourceRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eNone);
    graphCompCmdBuffers[currentFrame].end();

    // Submit work.
    vk::PipelineStageFlags graphicsWaitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo graphicsSubmitInfo = vk::SubmitInfo()
        .setCommandBuffers(graphCompCmdBuffers[currentFrame])
        .setWaitSemaphores(imageAquiredSemaphores[currentFrame])
        .setSignalSemaphores(renderFinishedSemaphores[imageIndex])
        .setWaitDstStageMask(graphicsWaitStage);
    graphicsComputeQueue.submit(graphicsSubmitInfo, inFlightFences[currentFrame]);
    
    // Present image.
    vk::PresentInfoKHR info = vk::PresentInfoKHR()
        .setSwapchains(swapchain.swapchain)
        .setImageIndices(imageIndex)
        .setWaitSemaphores(renderFinishedSemaphores[imageIndex]);
    try {
        graphicsComputeQueue.presentKHR(info);
    }
    catch (std::exception e) {
        requestNewSwapchain = true;
    }
    if (requestNewSwapchain) {
        requestNewSwapchain = false;
        device.device.waitForFences(inFlightFences[currentFrame], false, UINT64_MAX);
        device.device.resetFences(inFlightFences[currentFrame]);
        if(!freezeFrustum)
            vmaDestroyBuffer(allocator, stageBuffers[currentFrame].buffer, stageBuffers[currentFrame].alloc);
        device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
        swapchain.Recreate(instance.pWindow, doVsync);
        return;
    }

    currentFrame = (currentFrame + 1) % 2;
    device.device.waitForFences(inFlightFences[currentFrame], false, UINT64_MAX);
    device.device.resetFences(inFlightFences[currentFrame]);
    if (!freezeFrustum && !firstTime)
        vmaDestroyBuffer(allocator, stageBuffers[currentFrame].buffer, stageBuffers[currentFrame].alloc);
    graphCompCmdBuffers[currentFrame].reset();
    firstTime = false;
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
    imGuiInitInfo.Queue = static_cast<VkQueue>(graphicsComputeQueue);
    imGuiInitInfo.QueueFamily = device.graphicsComputeQueueFamilyIndex;
    imGuiInitInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 1;
    imGuiInitInfo.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    imGuiInitInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    imGuiInitInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = reinterpret_cast<VkFormat*>(&swapchain.renderFormat);
    imGuiInitInfo.MinAllocationSize = 1024 * 1024;
    ImGui_ImplVulkan_Init(&imGuiInitInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
    clearColorUI = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
}
void Renderer::CreatePipeline() {
    auto perspectiveRange = vk::PushConstantRange()
        .setOffset(0)
        .setSize(sizeof(PushConstantData))
        .setStageFlags(vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute);

    forwardShaders      = MakeTaskMeshShaderObjects(device.device, "shaders/triangle.task.spv", "shaders/triangle.mesh.spv", "shaders/forwardShading.frag.spv", dldid, perspectiveRange, descriptorLayouts);
    forwardPlusShaders  = MakeTaskMeshShaderObjects(device.device, "shaders/triangle.task.spv", "shaders/triangle.mesh.spv", "shaders/forwardPlusShading.frag.spv", dldid, perspectiveRange, descriptorLayouts);
    depthprepassShaders = MakeTaskMeshShaderObjects(device.device, "shaders/depthprepass.task.spv", "shaders/depthprepass.mesh.spv", "shaders/depthprepass.frag.spv", dldid, perspectiveRange, descriptorLayouts);
    lightCullingShader  = MakeComputeShaderObject(device.device, "shaders/lightCulling.comp.spv", dldid, perspectiveRange, descriptorLayouts);
    screenTileFrustumsShader = MakeComputeShaderObject(device.device, "shaders/screenTileFrustums.comp.spv", dldid, perspectiveRange, descriptorLayouts);

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
        .setPushConstantRanges(perspectiveRange)
        .setSetLayouts(descriptorLayouts);
    pipelineLayout = device.device.createPipelineLayout(pipelineLayoutInfo);
}
void Renderer::CreateFencesAndSemaphores() {
    auto semaphoreInfo = vk::SemaphoreCreateInfo();
    imageAquiredSemaphores  [0] = device.device.createSemaphore(semaphoreInfo);
    renderFinishedSemaphores[0] = device.device.createSemaphore(semaphoreInfo);
    imageAquiredSemaphores  [1] = device.device.createSemaphore(semaphoreInfo);
    renderFinishedSemaphores[1] = device.device.createSemaphore(semaphoreInfo);
    
    inFlightFences[1] = device.device.createFence(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
    inFlightFences[0] = device.device.createFence(vk::FenceCreateInfo());
    immediateFence    = device.device.createFence(vk::FenceCreateInfo());
}
void Renderer::InitMainObjects(SDL_Window* window, std::atomic<bool>* ready) {
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
        .setAspectMask(vk::ImageAspectFlagBits::eDepth)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);
    stencilSubresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eStencil)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);

    swapchain = Swapchain(&device.device, device.physicalDevice, instance.surface);
    for (auto& i : depthImages)
        i = CreateDepthImage();

    graphicsComputeQueue = device.device.getQueue(device.graphicsComputeQueueFamilyIndex, 0);

    graphicsComputeCommand = Command(device, device.graphicsComputeQueueFamilyIndex);
    graphCompCmdBuffers = graphicsComputeCommand.GetCommandBuffers();
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
void OptimizeMesh(std::vector<uint32_t>& indices, std::vector<Vertex>& vertices, std::vector<float>& positions) {
    std::vector<uint32_t> remap(indices.size());
    std::vector<uint32_t> newIndices(indices.size());

    size_t oldVertCount = vertices.size();
    size_t vertCount = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), oldVertCount, sizeof(Vertex));
    std::vector<Vertex> newVertices(vertCount);

    meshopt_remapIndexBuffer(newIndices.data(), indices.data(), indices.size(), remap.data());
    meshopt_remapVertexBuffer(newVertices.data(), vertices.data(), oldVertCount, sizeof(Vertex), remap.data());
    vertices = newVertices;
    indices = newIndices;

    std::cout << "Reduced " << oldVertCount - vertices.size()  << " Vertices\n";

    positions.resize(vertices.size() * 3);
    for (size_t i = 0; i < vertices.size(); i++) {
        positions[i * 3]     = vertices[i].Position.x;
        positions[i * 3 + 1] = vertices[i].Position.y;
        positions[i * 3 + 2] = vertices[i].Position.z;
    }

    meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), positions.data(), vertices.size(), sizeof(float) * 3, 1.05f);
    auto newVertSize = meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));

    positions.resize(newVertSize * 3);
    for (size_t i = 0; i < vertices.size(); i++) {
        positions[i * 3] = vertices[i].Position.x;
        positions[i * 3 + 1] = vertices[i].Position.y;
        positions[i * 3 + 2] = vertices[i].Position.z;
    }
}
void Renderer::LoadGLTF(std::filesystem::path path, glm::mat4 transform) {
    Timer total = Timer();
    Timer parts = Timer();
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (auto error = data.error(); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
    std::cout << "Took " << parts.GetMilliseconds() << " ms to open file." << "\n";
    parts.Reset();
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

    std::cout << "Took " << parts.GetMilliseconds() << " ms to load file." << "\n";
    parts.Reset();

    auto normalTransform = glm::mat3(glm::transpose(glm::inverse(transform)));

    for (const auto& node : asset.nodes) {
        // Load light.
        if (node.lightIndex.has_value()) {
            const auto& light = asset.lights[node.lightIndex.value()];
            const auto& nodeData = get<fastgltf::TRS>(node.transform);
            Light l;
            switch (light.type) {
            case fastgltf::LightType::Point:
                l.lightType = 0;
                l.color   = glm::vec3(light.color.x(), light.color.y(), light.color.z());
                l.pos     = glm::vec3(transform * glm::vec4(nodeData.translation.x(), nodeData.translation.y(), nodeData.translation.z(), 1));
                l.falloff = 0;
                l.radius  = 100;
                if (light.range.has_value())
                    l.radius = light.range.value();

                pointLights.emplace_back(l);
                break;
            case fastgltf::LightType::Spot:
                l.color       = glm::vec3(light.color.x(), light.color.y(), light.color.z());
                l.pos         = glm::vec3(transform * glm::vec4(nodeData.translation.x(), nodeData.translation.y(), nodeData.translation.z(), 1));
                //sl.lightDir = glm::fquat(nodeData.rotation.w(), nodeData.rotation.x(), nodeData.rotation.y(), nodeData.rotation.z());
                l.falloff     = 0;
                l.cutoff      = glm::radians(light.outerConeAngle.value());
                l.innerCutoff = glm::radians(light.innerConeAngle.value());
                
                l.radius = 100;
                if (light.range.has_value())
                    l.radius = light.range.value();
                //spotLights.emplace_back(sl);
                break;
            case fastgltf::LightType::Directional:
                l.color = glm::vec4(light.color.x(), light.color.y(), light.color.z(), 1);
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
    std::cout << "Took " << parts.GetMilliseconds() << " ms to load materials." << "\n";
    parts.Reset();

    // Load meshes.
    for (const auto& mesh : asset.meshes) {
        for (const auto& primitive : mesh.primitives) {
            MeshView meshView;
            meshView.start = vertices.size();

            std::vector<Vertex> verticesLocal;
            // Read vertices.
            {
                const auto& positions = ReadAttribute<glm::vec3>(asset, primitive, "POSITION");
                const auto& normals   = ReadAttribute<glm::vec3>(asset, primitive, "NORMAL");
                const auto& texCoords = ReadAttribute<glm::vec2>(asset, primitive, "TEXCOORD_0");

                verticesLocal.resize(positions.size());
                for (size_t i = 0; i < positions.size(); i++) {

                    verticesLocal[i] = { (transform * glm::vec4(positions[i], 1)).xyz, texCoords[i].x, glm::normalize(normalTransform * normals[i]), texCoords[i].y };
                }

                std::cout << "Took " << parts.GetMilliseconds() << " ms to add vertices." << "\n";
                parts.Reset();
            }

            // Determine material.
            meshView.material = 0;
            if (primitive.materialIndex.has_value())
                meshView.material = materialIDs[primitive.materialIndex.value()];

            // Load indices.
            auto& indIt = primitive.indicesAccessor;
            assert(indIt.has_value());

            const auto& indAcr        = asset.accessors[indIt.value()];
            const auto& indBufferView = asset.bufferViews[indAcr.bufferViewIndex.value()];
            const auto& indBuffer     = asset.buffers[indBufferView.bufferIndex];
            const auto& indData       = get<fastgltf::sources::Array>(indBuffer.data);
            
            std::vector<uint32_t> indices(indAcr.count);

            // If possible, the GLTF will use uint16 to reduce file size.
            if (indAcr.componentType == fastgltf::ComponentType::UnsignedShort) {
                std::vector<uint16_t> rawIndices(indAcr.count);
                std::memcpy(rawIndices.data(), indData.bytes.data() + indAcr.byteOffset + indBufferView.byteOffset, sizeof(uint16_t) * rawIndices.size());

                for (size_t i = 0; i < indAcr.count; i++)
                    indices[i] = static_cast<uint32_t>(rawIndices[i]);
            }
            else
                std::memcpy(&indices[0], indData.bytes.data() + indAcr.byteOffset + indBufferView.byteOffset, sizeof(uint32_t) * indices.size());

            std::vector<float> positions;
            OptimizeMesh(indices, verticesLocal, positions);

            auto prevVerticesSize = vertices.size();
            vertices.resize(prevVerticesSize + verticesLocal.size());
            std::memcpy(&vertices[prevVerticesSize], verticesLocal.data(), sizeof(Vertex) * verticesLocal.size());
            meshView.end   = vertices.size() - 1;
            meshView.flags = 1;

            AddMeshlets(indices, positions, prevVerticesSize, meshViews.size());
            meshViews.emplace_back(meshView);
        }
    }
    std::cout << "Took " << total.GetMilliseconds() << " ms to fully load model." << "\n\n";
}

void BuildMeshlets(std::span<uint32_t> indices, std::span<float> positions, 
    std::vector<meshopt_Meshlet>& meshlets, std::vector<uint32_t>& vertices, std::vector<uint8_t>& triangles, std::vector<MeshletBounds>& bounds, uint32_t meshID) {
    // TODO: Lower max values when mesh has fewer primitives.
    const size_t maxVertices = 64;
    const size_t maxTriangles = 124;
    const float  coneWeight = 0.25f;

    size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
    meshlets.resize(maxMeshlets);
    vertices.resize(maxMeshlets * maxVertices);
    triangles.resize(maxMeshlets * maxTriangles * 3);

    const size_t vertexCount = positions.size() / 3;
    size_t meshletCount = meshopt_buildMeshlets(&meshlets[0], &vertices[0], &triangles[0],
        indices.data(), indices.size(), positions.data(), vertexCount, sizeof(float) * 3, maxVertices, maxTriangles, coneWeight);

    const meshopt_Meshlet& lastElement = meshlets[meshletCount - 1];
    meshlets.resize(meshletCount);
    vertices.resize(lastElement.vertex_offset + lastElement.vertex_count);
    triangles.resize(lastElement.triangle_offset + ((lastElement.triangle_count * 3 + 3) & ~3));

    for (auto& m : meshlets)
        meshopt_optimizeMeshlet(&vertices[m.vertex_offset], &triangles[m.triangle_offset], m.triangle_count, m.vertex_count);

    bounds.resize(meshlets.size());
    for (size_t i = 0; i < bounds.size(); i++) {
        auto& m = meshlets[i];
        const auto bound = meshopt_computeMeshletBounds(&vertices[m.vertex_offset], &triangles[m.triangle_offset], m.triangle_count, positions.data(), vertexCount, sizeof(float) * 3);

        auto coneDir = glm::vec3(bound.cone_axis[0], bound.cone_axis[1], bound.cone_axis[2]);
        bounds[i] = {
            glm::vec3(bound.center[0], bound.center[1], bound.center[2]),
            bound.radius,
            glm::vec3(bound.cone_apex[0], bound.cone_apex[1], bound.cone_apex[2]),
            bound.cone_cutoff,
            coneDir,
            meshID
        };
    }
}

void Renderer::AddMeshlets(std::span<uint32_t> indices, std::span<float> positions, uint32_t prevVerticesSize, uint32_t meshID) {
    std::vector<meshopt_Meshlet> meshletsLocal;
    std::vector<uint32_t>        verticesLocal;
    std::vector<uint8_t>         indicesLocal;
    std::vector<MeshletBounds>   boundsLocal;
    BuildMeshlets(indices, positions, meshletsLocal, verticesLocal, indicesLocal, boundsLocal, meshID);

    // Add to geometry pool.
    auto prevMeshletsSize = meshlets.size();
    auto prevMeshletIndicesSize  = meshletTriangles.size();
    auto prevMeshletVerticesSize = meshletVertices.size();
    auto prevMeshletBoundsSize   = meshletBounds.size();

    meshlets.resize(meshletsLocal.size() + prevMeshletsSize);
    for (size_t i = 0; i < meshletsLocal.size(); i++) {
        auto& m = meshletsLocal[i];
        meshlets[i + prevMeshletsSize].triangle_offset = m.triangle_offset + prevMeshletIndicesSize;
        meshlets[i + prevMeshletsSize].vertex_offset   = m.vertex_offset   + prevMeshletVerticesSize;
        meshlets[i + prevMeshletsSize].triangle_count  = m.triangle_count;
        meshlets[i + prevMeshletsSize].vertex_count    = m.vertex_count;
    }

    meshletVertices .resize(prevMeshletVerticesSize + verticesLocal.size());
    meshletTriangles.resize(prevMeshletIndicesSize  + indicesLocal.size());
    meshletBounds   .resize(prevMeshletBoundsSize   + boundsLocal.size());
    for (size_t i = 0; i < verticesLocal.size(); i++)
        meshletVertices[i + prevMeshletVerticesSize] = verticesLocal[i] + prevVerticesSize;

    std::memcpy(&meshletTriangles[meshletTriangles.size() - indicesLocal.size()], indicesLocal.data(), sizeof(uint8_t) * indicesLocal.size());
    std::memcpy(&meshletBounds[meshletBounds.size() - boundsLocal.size()], boundsLocal.data(), sizeof(MeshletBounds) * boundsLocal.size());
}

template<typename T>
GPUBuffer Renderer::UploadData(std::span<T> data) {
    const auto size = sizeof(T) * data.size();

    auto buffer = CreateBuffer(size, vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);
    auto addressInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(buffer.buffer);

    auto stageBuffer = CreateBuffer(size,
        vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    auto byteData = static_cast<std::byte*>(stageBuffer.alloc->GetMappedData());
    std::memcpy(byteData, data.data(), size);

    std::function<void()> func = [&]() {
        auto region = vk::BufferCopy()
            .setSize(size);
        graphCompCmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, buffer.buffer, region);
        };
    SubmitImmediate(func);
    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
    vmaDestroyBuffer(allocator, stageBuffer.buffer, stageBuffer.alloc);

    return GPUBuffer{ buffer, device.device.getBufferAddress(addressInfo) };
}
template<typename T>
GPUBuffer Renderer::UploadData(T&& data) {
    const auto size = sizeof(T);

    auto buffer = CreateBuffer(size, vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);
    auto addressInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(buffer.buffer);

    auto stageBuffer = CreateBuffer(size,
        vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    auto byteData = static_cast<std::byte*>(stageBuffer.alloc->GetMappedData());
    std::memcpy(byteData, &data, size);

    std::function<void()> func = [&]() {
        auto region = vk::BufferCopy()
            .setSize(size);
        graphCompCmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, buffer.buffer, region);
        };
    SubmitImmediate(func);
    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
    vmaDestroyBuffer(allocator, stageBuffer.buffer, stageBuffer.alloc);

    return GPUBuffer{ buffer, device.device.getBufferAddress(addressInfo) };
}

template<typename T>
void Renderer::UpdateBuffer(GPUBuffer& buffer, std::span<T> data, AllocatedBuffer& stageBuffer, size_t offset) {
    const auto size = sizeof(T) * data.size();

    stageBuffer = CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    auto byteData = static_cast<std::byte*>(stageBuffer.alloc->GetMappedData());
    std::memcpy(byteData, data.data(), size);

    std::function<void()> func = [&]() {
        auto region = vk::BufferCopy()
            .setSize(size)
            .setDstOffset(offset);
        graphCompCmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, buffer.buffer.buffer, region);
    };

    //SubmitImmediate(func);
    func();
}
AllocatedBuffer Renderer::CreateBuffer(size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage) {
    VkBufferCreateInfo bufferInfo = vk::BufferCreateInfo()
        .setSize(allocSize)
        .setUsage(usage);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    AllocatedBuffer allocBuffer;
    VkBuffer buffer;
    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocBuffer.alloc, &allocBuffer.info);
    
    allocBuffer.buffer = vk::Buffer(buffer);
    return allocBuffer;
}
GPUBuffer Renderer::UploadMesh(std::span<Vertex> vertices) {
    // GPU only buffers.
    const size_t vertSize = vertices.size() * sizeof(Vertex);

    auto vertBuffer = CreateBuffer(vertSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);

    auto vertInfo = vk::BufferDeviceAddressInfo()
        .setBuffer(vertBuffer.buffer);

    GPUBuffer meshbuffer{
        vertBuffer,
        device.device.getBufferAddress(vertInfo)
    };

    // Temporary CPU buffer for sending data.
    AllocatedBuffer stageBuffer = CreateBuffer(vertSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    auto data = static_cast<std::byte*>(stageBuffer.alloc->GetMappedData());

    std::memcpy(data, vertices.data(), vertSize);

    std::function<void()> func = [&]() {
        auto vertRegion = vk::BufferCopy()
            .setSize(vertSize);
        graphCompCmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, meshbuffer.buffer.buffer, vertRegion);
    };
    SubmitImmediate(func);

    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
    vmaDestroyBuffer(allocator, stageBuffer.buffer, stageBuffer.alloc);

    return meshbuffer;
}

AllocatedImage Renderer::CreateDepthImage() {
    auto depthStencilImage = CreateDepthStencilImages(swapchain.renderExtend, depthSubresourceRange, stencilSubresourceRange);
    return depthStencilImage[0];
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
std::array<AllocatedImage, 2> Renderer::CreateDepthStencilImages(vk::Extent2D extend, vk::ImageSubresourceRange depthSubresource, vk::ImageSubresourceRange stencilSubresource) {
    // Get supported depth format.
    std::array<vk::Format, 3> depthFormats = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint
    };
    vk::Format depthStencilFormat;
    for (auto& f : depthFormats) {
        auto properties = device.physicalDevice.getFormatProperties(f);
        if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            depthStencilFormat = f;
            break;
        }
    }

    uint32_t mipLevelCount = 1;
    //if (makeMipmaps)
    //    mipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(extend.height, extend.width)))) + 1;

    std::array<uint32_t, 2> queueFamilyIndices = {
        device.graphicsComputeQueueFamilyIndex,
        device.computeQueueFamilyIndex
    };

    auto imageInfo = vk::ImageCreateInfo()
        .setArrayLayers(1)
        .setExtent(vk::Extent3D(extend, 1))
        .setFlags(vk::ImageCreateFlags())
        .setFormat(depthStencilFormat/*/vk::Format::eD24UnormS8Uint*/)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setMipLevels(mipLevelCount)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eConcurrent)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setImageType(vk::ImageType::e2D)
        .setTiling(vk::ImageTiling::eOptimal)
        .setQueueFamilyIndices(queueFamilyIndices);

    VmaAllocationCreateInfo imageAllocCreateInfo = {};
    imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    imageAllocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    vk::Image image;
    VmaAllocation alloc;
    auto result = vmaCreateImage(allocator, reinterpret_cast<VkImageCreateInfo*>(&imageInfo), &imageAllocCreateInfo, reinterpret_cast<VkImage*>(&image), &alloc, nullptr);

    vk::ImageView depthImageView, stencilImageView;
    depthImageView = CreateImageView(image, depthStencilFormat, depthSubresource);
    stencilImageView = CreateImageView(image, depthStencilFormat, stencilSubresource);

    AllocatedImage depthImage = { image, depthImageView, alloc };
    AllocatedImage stencilImage = { image, stencilImageView, alloc };
    return { depthImage, stencilImage };
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
        TransitionImage(graphCompCmdBuffers[currentFrame], image.image, swapchain.subresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
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

        graphCompCmdBuffers[currentFrame].copyBufferToImage(upload.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, imageCopy);
        TransitionImage(graphCompCmdBuffers[currentFrame], image.image, swapchain.subresourceRange, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderSampledRead);
    };
    SubmitImmediate(func);
    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);

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

void Renderer::CreateDebugTextures() {
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    std::array<uint32_t, 16 * 16> checkerboardData;
    for (size_t x = 0; x < 16; x++)
        for (size_t y = 0; y < 16; y++)
            checkerboardData[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
    textures.emplace_back(CreateUploadImage(checkerboardData.data(), vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ 16, 16 }, vk::ImageUsageFlagBits::eSampled));
    textures.emplace_back(CreateUploadImage(&black, vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ 1, 1 }, vk::ImageUsageFlagBits::eSampled));
    textures.emplace_back(CreateUploadImage(&white, vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ 1, 1 }, vk::ImageUsageFlagBits::eSampled));
    materialIndexGroups.emplace_back(0, 1, 1);
}

// Temporary functions.
void Renderer::PushConstant_Draw() {
    SceneInfo sceneInfo;
    sceneInfo.pointLightCount = pointLights.size();
    sceneInfo.spotLightCount  = spotLights.size();
    sceneInfo.dirLightCount   = dirLights.size();
    sceneInfo.meshCount    = meshViews.size();
    sceneInfo.windowWidth  = (float)swapchain.renderExtend.width;
    sceneInfo.windowHeight = (float)swapchain.renderExtend.height;
    sceneInfo.tileCountX   = (uint32_t)((swapchain.renderExtend.width  + (swapchain.renderExtend.width  % 16)) / 16);
    sceneInfo.tileCountY   = (uint32_t)((swapchain.renderExtend.height + (swapchain.renderExtend.height % 16)) / 16);

    PushConstantData pushConstant{
        projViewTransform,
        view,
        proj,
        glm::vec4(position, 1),
        sceneInfo
    };
    graphCompCmdBuffers[currentFrame].pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstantData), &pushConstant);
}
void Renderer::ImGui_Draw(double frameTime) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::Text(std::format("X: {:.4f} Y: {:.4f} Z {:.4f}", position.x, position.y, position.z).c_str());
    ImGui::Text(std::format("{:.3f} ms | {:.1f} fps\n", frameTime, 1000 / frameTime).c_str());
    requestNewSwapchain = ImGui::Checkbox("Toggle Vsync", &doVsync);
    ImGui::Checkbox("Freeze frustum", &freezeFrustum);
    ImGui::Checkbox("Use Forward Plus shading", &doLightCulling);
}
void Renderer::LoadModels_Init() {
    parser = fastgltf::Parser(fastgltf::Extensions::KHR_lights_punctual);

    auto helmetTrans = glm::mat4(1.0f);
    helmetTrans = glm::translate(helmetTrans, glm::vec3(-5.0f, 0, 0));
    helmetTrans = glm::rotate<float>(helmetTrans, glm::radians(90.0f), glm::vec3(-1, 0, 0));
    LoadGLTF("assets/DamagedHelmet.glb", helmetTrans);
    
    auto dragonTrans = glm::mat4(1.0f);
    dragonTrans = glm::translate(dragonTrans, glm::vec3(5.0f, 5.0f, 2.0f));
    dragonTrans = glm::rotate<float>(dragonTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
    dragonTrans = glm::scale(dragonTrans, glm::vec3(0.1f));
    LoadGLTF("assets/stanford_dragon.glb", dragonTrans);
    
    auto toyTrans = glm::mat4(1.0f);
    toyTrans = glm::translate(toyTrans, glm::vec3(-3.0f, 0, 0));
    toyTrans = glm::rotate<float>(toyTrans, glm::radians(90.0f), glm::vec3(-1, 0, 0));
    toyTrans = glm::scale(toyTrans, glm::vec3(0.005f));
    LoadGLTF("assets/ToyCar.glb", toyTrans);
    
    auto monkeTrans = glm::mat4(1.0f);
    monkeTrans = glm::translate(monkeTrans, glm::vec3(-2, -4, 3));
    monkeTrans = glm::rotate(monkeTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
    LoadGLTF("assets/monke.glb", monkeTrans);

    //Many sponzas for benchmarking.
    for (size_t i = 0; i < 1; i++) {
        for (size_t j = 0; j < 1; j++) {
            for (size_t k = 0; k < /*3*/1; k++) {
                auto sponzaTrans = glm::mat4(1.0f);
                sponzaTrans = glm::translate(sponzaTrans, glm::vec3(i * 40, j * 20, k * 25));
                sponzaTrans = glm::rotate<float>(sponzaTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
                sponzaTrans = glm::scale(sponzaTrans, glm::vec3(0.01f));
                LoadGLTF("assets/sponza.glb", sponzaTrans);
            }
        }
    }

    std::cout << "\nLoaded all models.\n";
    std::cout << "Size of all vertices: " << sizeof(Vertex) * vertices.size() << " Bytes\n";
}
void Renderer::SpawnLights_Init() {
    // xyz: 20 0 25 "Centre"
    const auto centre = glm::vec3(20, 0, 25);
    std::random_device randomDevice;
    auto ranGen = std::mt19937(3529725061);

    std::uniform_int_distribution<int> posxzDistrib(-10, 60);
    std::uniform_int_distribution<int> posyDistrib(-10, 20);
    std::uniform_int_distribution<int> rangeDistrib(5, 30);
    std::uniform_real_distribution<float> colorDistrib(0, 1);
    for (size_t i = 0; i < 100; i++) {
        const auto pos = glm::vec3(posxzDistrib(ranGen), posyDistrib(ranGen), posxzDistrib(ranGen));
        const auto dir = centre - pos;

        Light sl;
        sl.pos         = pos;
        sl.radius      = rangeDistrib(ranGen);
        sl.lightDir    = glm::vec4(glm::normalize(dir), 1);
        sl.color       = glm::vec3(1);
        sl.falloff     = 0.0f;
        sl.cutoff      = 0.95f;
        sl.innerCutoff = 0.96f;
        sl.lightType   = 1;
        spotLights.emplace_back(sl);
    }
    for (size_t i = 0; i < 100; i++) {
        const auto pos = glm::vec3(posxzDistrib(ranGen), posyDistrib(ranGen), posxzDistrib(ranGen));
        const auto dir = centre - pos;

        Light pl;
        pl.pos = pos;
        pl.radius = rangeDistrib(ranGen);
        pl.color = glm::vec3(colorDistrib(ranGen), colorDistrib(ranGen), colorDistrib(ranGen));
        pl.falloff = 0.0f;
        pl.lightType = 0;
        pointLights.emplace_back(pl);
    }
    Light pl;
    pl.pos     = glm::vec3(20.0f, 0.0f, 0.0f);
    pl.radius  = 25.0f;
    pl.color   = glm::vec3(0.0f, 0.2f, 0.5f);
    pl.falloff = 10.0f;
    pl.lightType = 0;
    pointLights.emplace_back(pl);

    Light dl;
    dl.color     = glm::vec3(0.85f, 0.85f, 0.5f);
    dl.lightDir  = glm::vec4(-1.0f, 1.0f, -1.0f, 1);
    dl.lightType = 2;
    dirLights.emplace_back(dl);
}
void Renderer::UploadAll_Init() {
    // Upload geometry and material indices.
    if (vertices.size() > 0)
        meshBuffer = UploadMesh(vertices);
    if (meshViews.size() > 0)
        meshViewBufferAddress = UploadData<MeshView>(meshViews);
    if (meshletBounds.size() > 0)
        meshletBoundsAddress = UploadData<MeshletBounds>(meshletBounds);
    if (meshlets.size() > 0)
        meshletsAddress = UploadData<meshopt_Meshlet>(meshlets);
    if (meshletVertices.size() > 0)
        meshletVerticesAddress = UploadData<uint32_t>(meshletVertices);
    if (meshletTriangles.size() > 0)
        meshletTrianglesAddress = UploadData<uint8_t>(meshletTriangles);

    // Upload materials.
    if (materialIndexGroups.size() > 0)
        materialBufferAddress   = UploadData<MaterialIndexGroup>(materialIndexGroups);
    
    lights.resize(pointLights.size() + spotLights.size() + dirLights.size());
    size_t lightMemIndex = 0;
    if(pointLights.size() > 0)
        std::memcpy(&lights[lightMemIndex], pointLights.data(), pointLights.size() * sizeof(Light));
    lightMemIndex += pointLights.size();
    if (spotLights.size() > 0)
        std::memcpy(&lights[lightMemIndex], spotLights.data(), spotLights.size() * sizeof(Light));
    lightMemIndex += spotLights.size();
    if (dirLights.size() > 0)
        std::memcpy(&lights[lightMemIndex], dirLights.data(), dirLights.size() * sizeof(Light));

    if (lights.size() > 0)
        lightBufferAddress = UploadData<Light>(lights);
}
void Renderer::CreateSamplers_Init() {
    auto nearestSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest);
    auto linearSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear);
    auto nearLinSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest);
    auto linNearSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear);

    nearestSampler = device.device.createSampler(nearestSamplerInfo);
    linearSampler = device.device.createSampler(linearSamplerInfo);
}
void Renderer::CreateDescSets_Init() {
    std::vector<vk::DescriptorSetLayoutBinding> layoutBindings = {
        vk::DescriptorSetLayoutBinding()
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(textures.size())
        .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding()
        .setBinding(1)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        vk::DescriptorSetLayoutBinding()
        .setBinding(2)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding()
        .setBinding(3)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eAll),
        vk::DescriptorSetLayoutBinding()
        .setBinding(4)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
    };
    std::vector<vk::DescriptorPoolSize> descPoolSizes = {
        vk::DescriptorPoolSize()
        .setType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(textures.size()),
        vk::DescriptorPoolSize()
        .setType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1),
        vk::DescriptorPoolSize()
        .setType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1),
        vk::DescriptorPoolSize()
        .setType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1),
        vk::DescriptorPoolSize()
        .setType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
    };
    
    auto descriptorLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
        .setBindings(layoutBindings);
    descriptorLayouts.emplace_back(device.device.createDescriptorSetLayout(descriptorLayoutInfo));
    
    auto descPoolInfo = vk::DescriptorPoolCreateInfo()
        .setMaxSets(1)
        .setPoolSizes(descPoolSizes);
    auto descPool = device.device.createDescriptorPool(descPoolInfo);
    
    // Descriptor sets.
    auto descAlloc = vk::DescriptorSetAllocateInfo()
        .setDescriptorPool(descPool)
        .setSetLayouts(descriptorLayouts);
    descriptorSets = device.device.allocateDescriptorSets(descAlloc);
    
    // Buffers.
    lightIndicesSize = MAX_LIGHTS_PER_TILE * sizeof(int) * std::ceil(swapchain.renderExtend.height / 16) * std::ceil(swapchain.renderExtend.width / 16);
    lightIndicesBuffer  = CreateBuffer(lightIndicesSize, vk::BufferUsageFlagBits::eStorageBuffer, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    tileFrustumsSize = sizeof(glm::vec4) * 6 * std::ceil(swapchain.renderExtend.height / 16) * std::ceil(swapchain.renderExtend.width / 16);
    tileFrustumBuffer = CreateBuffer(tileFrustumsSize, vk::BufferUsageFlagBits::eStorageBuffer, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    // Upload buffer addresses.
    bufferAddresses.meshletsAddress = meshletsAddress.bufferAddress;
    bufferAddresses.meshletVerticesAddress = meshletVerticesAddress.bufferAddress;
    bufferAddresses.meshletTrianglesAddress = meshletTrianglesAddress.bufferAddress;
    bufferAddresses.meshletBoundsAddress = meshletBoundsAddress.bufferAddress;

    bufferAddresses.meshViewBufferAddress = meshViewBufferAddress.bufferAddress;
    bufferAddresses.vertexBufferAddress = meshBuffer.bufferAddress;
    bufferAddresses.materialBufferAddress = materialBufferAddress.bufferAddress;
    bufferAddresses.lightBufferAddress = lightBufferAddress.bufferAddress;

    bufferAddressBuffer.buffer = CreateBuffer(lightIndicesSize, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    std::vector<BufferAddresses> bufferAddressesVec = { bufferAddresses };
    std::function<void()> addresses = [&]() { UpdateBuffer<BufferAddresses>(bufferAddressBuffer, bufferAddressesVec, stageBuffers[0]); };
    SubmitImmediate(addresses);

    // Descriptors.
    std::vector<vk::DescriptorImageInfo> imageDescriptors;
    imageDescriptors.reserve(textures.size());
    for (size_t i = 0; i < textures.size(); i++) {
        auto imageDescriptor = vk::DescriptorImageInfo()
            .setSampler(nearestSampler)
            .setImageLayout(vk::ImageLayout::eAttachmentOptimal)
            .setImageView(textures[i].view);
    
        imageDescriptors.emplace_back(imageDescriptor);
    }
    auto depthDescriptor = vk::DescriptorImageInfo()
        .setSampler(nearestSampler)
        .setImageLayout(vk::ImageLayout::eDepthReadOnlyOptimal)
        .setImageView(depthImages[0].view);
    auto lightIndicesDescriptor = vk::DescriptorBufferInfo()
        .setBuffer(lightIndicesBuffer.buffer)
        .setRange(lightIndicesSize);
    auto bufferAddressDescriptor = vk::DescriptorBufferInfo()
        .setBuffer(bufferAddressBuffer.buffer.buffer)
        .setRange(sizeof(BufferAddresses));
    auto frustumBufferDescriptor = vk::DescriptorBufferInfo()
        .setBuffer(tileFrustumBuffer.buffer)
        .setRange(tileFrustumsSize);

    std::vector<vk::WriteDescriptorSet> descWrite = {
        vk::WriteDescriptorSet()
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDstSet(descriptorSets[0])
        .setDstBinding(0)
        .setDescriptorCount(imageDescriptors.size())
        .setImageInfo(imageDescriptors),
        vk::WriteDescriptorSet()
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDstSet(descriptorSets[0])
        .setDstBinding(1)
        .setDescriptorCount(1)
        .setImageInfo(depthDescriptor),
        vk::WriteDescriptorSet()
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDstSet(descriptorSets[0])
        .setDstBinding(2)
        .setDescriptorCount(1)
        .setBufferInfo(lightIndicesDescriptor),
        vk::WriteDescriptorSet()
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDstSet(descriptorSets[0])
        .setDstBinding(3)
        .setDescriptorCount(1)
        .setBufferInfo(bufferAddressDescriptor),
        vk::WriteDescriptorSet()
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDstSet(descriptorSets[0])
        .setDstBinding(4)
        .setDescriptorCount(1)
        .setBufferInfo(frustumBufferDescriptor)
    };
    
    std::function<void()> descFunc = [&]() { device.device.updateDescriptorSets(descWrite, nullptr); };
    SubmitImmediate(descFunc);
    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
}