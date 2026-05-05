#define STB_IMAGE_IMPLEMENTATION
#define VMA_IMPLEMENTATION

#include "Renderer.h"

#include "simdjson.h"

Renderer::Renderer(SDL_Window* window, std::atomic<bool>* ready) {
    InitMainObjects(window, ready);
    CreateFencesAndSemaphores();

    CreateSamplers_Init();
    CreateDebugTextures();

    vertices.resize(6);

    LoadModels_Init();
    SpawnLights_Init();

    CreateDescSets_Init();
    CreatePipeline();

    // Setup UI.
    InitImGui(window);
}

void Renderer::Draw() {
    uint32_t imageIndex;
    if (!AcquireImageIndex(imageIndex)) return;

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
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
        graphCompCmdBuffers[currentFrame].dispatch(lightCullX, lightCullY, 1);
        
        const auto tileFrustumsBarrier = vk::BufferMemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderStorageRead)
            .setBuffer(tileFrustumBuffer.buffer)
            .setSize(tileFrustumsSize);

        const auto tileBarriers = { tileFrustumsBarrier };
        const auto tileFrustumsDependencyInfo = vk::DependencyInfo()
            .setBufferMemoryBarriers(tileBarriers);
        
        graphCompCmdBuffers[currentFrame].pipelineBarrier2(tileFrustumsDependencyInfo);
    }

    const auto dirShadowMap = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearDepthStencilValue(0, 0))
        .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
        .setImageView(shadowMapImage.view)
        .setResolveMode(vk::ResolveModeFlagBits::eNone)
        .setResolveImageLayout(vk::ImageLayout::eUndefined);

    graphCompCmdBuffers[currentFrame].setDepthTestEnable(vk::True);
    graphCompCmdBuffers[currentFrame].setDepthWriteEnable(vk::True);
    graphCompCmdBuffers[currentFrame].setDepthCompareOp(vk::CompareOp::eGreater);

    // Directional shadow map pass.
    {
        constexpr auto shadowArea = vk::Rect2D()
        .setExtent(vk::Extent2D(SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION));
        const auto shadowRenderInfo = vk::RenderingInfo()
        .setPDepthAttachment(&dirShadowMap)
        .setRenderArea(shadowArea)
        .setFlags(vk::RenderingFlags())
        .setLayerCount(1);
        graphCompCmdBuffers[currentFrame].beginRendering(shadowRenderInfo);
        {
            graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, shadowMapShaders, dldid);
            graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
            graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
        }
        graphCompCmdBuffers[currentFrame].endRendering();
    }
    const auto shadowBarrier = vk::ImageMemoryBarrier2()
        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
        .setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
        .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
        .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
        .setNewLayout(vk::ImageLayout::eReadOnlyOptimal)
        .setOldLayout(vk::ImageLayout::eAttachmentOptimal)
        .setImage(shadowMapImage.image)
        .setSubresourceRange(depthSubresourceRange);
    const auto shadowDependencyInfo = vk::DependencyInfo()
        .setImageMemoryBarriers(shadowBarrier);
    graphCompCmdBuffers[currentFrame].pipelineBarrier2(shadowDependencyInfo);

    // Depth prepass.
    vk::RenderingInfo renderInfo(vk::RenderingFlags(), renderArea, 1, 0, colorAttachment, &depthAttachment);
    graphCompCmdBuffers[currentFrame].beginRendering(renderInfo);
    {
        graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, depthprepassShaders, dldid);
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
        graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
    
        graphCompCmdBuffers[currentFrame].setDepthWriteEnable(vk::False);
        graphCompCmdBuffers[currentFrame].setDepthCompareOp(vk::CompareOp::eEqual);
    }
    graphCompCmdBuffers[currentFrame].endRendering();
    renderInfo.setFlags(vk::RenderingFlagBits::eResuming);

    // Light culling.
    if (doLightCulling) {
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptors.descriptorSets, nullptr);

        if (useForwardPlusTestShader) {
            graphCompCmdBuffers[currentFrame].bindShadersEXT(vk::ShaderStageFlagBits::eCompute, lightCullingViewShader, dldid);
            graphCompCmdBuffers[currentFrame].dispatch(1, 1, 1);

            const auto lightInViewBarrier = vk::BufferMemoryBarrier2()
                .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
                .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
                .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
                .setDstAccessMask(vk::AccessFlagBits2::eShaderStorageRead)
                .setBuffer(lightIndicesViewBuffer.buffer)
                .setSize(lightIndicesViewSize);

            const auto lightInViewDependencyInfo = vk::DependencyInfo()
                .setBufferMemoryBarriers(lightInViewBarrier);
            graphCompCmdBuffers[currentFrame].pipelineBarrier2(lightInViewDependencyInfo);
        }

        const auto depthToComputeBarrier = vk::ImageMemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
            .setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
            .setOldLayout(vk::ImageLayout::eAttachmentOptimal)
            .setNewLayout(vk::ImageLayout::eReadOnlyOptimal)
            .setImage(depthImage.image)
            .setSubresourceRange(depthStencilSubresourceRange);
        const auto depthToComputeDependencyInfo = vk::DependencyInfo()
            .setImageMemoryBarriers(depthToComputeBarrier);
        graphCompCmdBuffers[currentFrame].pipelineBarrier2(depthToComputeDependencyInfo);

        uint32_t lightCullX = (swapchain.renderExtend.width  + (swapchain.renderExtend.width  % 16)) / 16;
        uint32_t lightCullY = (swapchain.renderExtend.height + (swapchain.renderExtend.height % 16)) / 16;
        if(useForwardPlusTestShader)
            graphCompCmdBuffers[currentFrame].bindShadersEXT(vk::ShaderStageFlagBits::eCompute, lightCullingTestShader, dldid);
        else
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
            .setImage(depthImage.image)
            .setSubresourceRange(depthStencilSubresourceRange);
    
        const auto lightCullingDependencyInfo = vk::DependencyInfo()
            .setImageMemoryBarriers(depthToGraphicsBarrier)
            .setBufferMemoryBarriers(lightIndicesBarrier);
        graphCompCmdBuffers[currentFrame].pipelineBarrier2(lightCullingDependencyInfo);
    }

    // Forward shading and specular.
    graphCompCmdBuffers[currentFrame].beginRendering(renderInfo);
    if (doLightCulling) {
        if (!useForwardPlusTestShader) {
            graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, forwardPlusShaders, dldid);
            graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
            graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
        } else if (showLightHeatmap) {
            graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, lightHeatmapShaders, dldid);
            graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
            graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
        }
        else {
            graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, forwardPlusTestShaders, dldid);
            graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
            graphCompCmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
        }
    }
    else {
        graphCompCmdBuffers[currentFrame].bindShadersEXT(meshStages, forwardShaders, dldid);
        graphCompCmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptors.descriptorSets, nullptr);
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
    for (auto & plane : planes) {
        const auto length = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        plane /= length;
    }

    return planes;
}
void Renderer::Move(const float forward, const float sideward) {
    position += forward * direction;
    position -= glm::normalize(glm::cross(direction, glm::vec3(0, 1, 0))) * sideward;
}
void Renderer::Teleport(const glm::vec3 pos, const glm::vec3 dir) {
    position = pos;
    yaw = -90;
    pitch = 0;
}
void Renderer::BuildGlobalTransform() {
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction = glm::normalize(direction);

    const float ratio = static_cast<float>(swapchain.renderExtend.width) / static_cast<float>(swapchain.renderExtend.height);
    constexpr float near  = 0.01f;
    constexpr float far   = 4000.0f;
    constexpr float fovY  = 90.0f;
    constexpr auto up     = glm::vec3(0, 1.0f, 0);

    view = glm::lookAt(position, position + direction, up);

    // Near and far are swapped to create a reverse z.
    proj = glm::perspective(glm::radians(fovY), ratio, far, near);
    projViewTransform = {
        proj * view
    };
    const auto frustum = ExtractFrustum(projViewTransform);
    for (int i = 0; i < 6; i++) {
        vertices[i] = { glm::vec3(frustum[i].x, frustum[i].y, frustum[i].z), frustum[i].w, glm::vec3(0), 0 };
    }
}

bool Renderer::AcquireImageIndex(uint32_t& index) {
    const auto imageNext   = device.device.acquireNextImageKHR(swapchain.Get(), UINT64_MAX, imageAcquiredSemaphores[currentFrame], nullptr);
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
        .setClearValue(vk::ClearDepthStencilValue(0, 0))
        .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
        .setImageView(depthImage.view)
        .setResolveMode(vk::ResolveModeFlagBits::eNone)
        .setResolveImageLayout(vk::ImageLayout::eUndefined);
    colorAttachment = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue({ 0.0f, 0.0f, 0.0f, 0.0f }))
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setImageView(swapchain.imageViews[imageIndex])
        .setResolveMode(vk::ResolveModeFlagBits::eNone);

    auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    graphCompCmdBuffers[currentFrame].begin(beginInfo);

    TransitionImage(graphCompCmdBuffers[currentFrame], swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, vk::AccessFlagBits2::eNone,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    TransitionImage(graphCompCmdBuffers[currentFrame], depthImage.image, depthStencilSubresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AccessFlagBits2::eNone,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    TransitionImage(graphCompCmdBuffers[currentFrame], shadowMapImage.image, depthSubresourceRange, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal, vk::AccessFlagBits2::eNone,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    SetDynamicStates(graphCompCmdBuffers[currentFrame], dldid);

    const auto viewport = vk::Viewport()
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f)
        .setHeight(-static_cast<float>(swapchain.renderExtend.height))
        .setWidth(swapchain.renderExtend.width)
        .setX(0)
        .setY(swapchain.renderExtend.height);
    graphCompCmdBuffers[currentFrame].setViewportWithCount(viewport);
    const auto scissor = vk::Rect2D()
        .setExtent(swapchain.renderExtend)
        .setOffset({ 0 ,0 });
    graphCompCmdBuffers[currentFrame].setScissorWithCount(scissor);

    renderArea = vk::Rect2D()
        .setExtent(swapchain.renderExtend);

    BuildGlobalTransform();
}
void Renderer::SubmitImmediate(const std::function<void()>& func) {
    device.device.resetFences(immediateFence);

    constexpr auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    graphCompCmdBuffers[0].begin(beginInfo);
    graphCompCmdBuffers[1].begin(beginInfo);

    func();

    graphCompCmdBuffers[0].end();
    graphCompCmdBuffers[1].end();

    const auto graphicsSubmitInfo = vk::SubmitInfo()
        .setCommandBuffers(graphCompCmdBuffers);
    graphicsComputeQueue.submit(graphicsSubmitInfo, immediateFence);
    {const auto res = device.device.waitForFences(immediateFence, false, UINT64_MAX);}
}
void Renderer::SubmitAndPresent(uint32_t imageIndex) {
    TransitionImage(graphCompCmdBuffers[currentFrame], swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eNone);
    TransitionImage(graphCompCmdBuffers[currentFrame], depthImage.image, depthStencilSubresourceRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eNone);
    TransitionImage(graphCompCmdBuffers[currentFrame], shadowMapImage.image, depthSubresourceRange, vk::ImageLayout::eReadOnlyOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eNone);
    graphCompCmdBuffers[currentFrame].end();

    // Submit work.
    vk::PipelineStageFlags graphicsWaitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo graphicsSubmitInfo = vk::SubmitInfo()
        .setCommandBuffers(graphCompCmdBuffers[currentFrame])
        .setWaitSemaphores(imageAcquiredSemaphores[currentFrame])
        .setSignalSemaphores(renderFinishedSemaphores[imageIndex])
        .setWaitDstStageMask(graphicsWaitStage);
    graphicsComputeQueue.submit(graphicsSubmitInfo, inFlightFences[currentFrame]);
    
    // Present image.
    const vk::PresentInfoKHR info = vk::PresentInfoKHR()
        .setSwapchains(swapchain.swapchain)
        .setImageIndices(imageIndex)
        .setWaitSemaphores(renderFinishedSemaphores[imageIndex]);
    try {
        const auto res = graphicsComputeQueue.presentKHR(info);
    }
    catch ([[maybe_unused]] std::exception& e) {
        requestNewSwapchain = true;
    }
    if (requestNewSwapchain) {
        requestNewSwapchain = false;
        {const auto res = device.device.waitForFences(inFlightFences[currentFrame], false, UINT64_MAX);}
        device.device.resetFences(inFlightFences[currentFrame]);
        if(!freezeFrustum)
            vmaDestroyBuffer(allocator, stageBuffers[currentFrame].buffer, stageBuffers[currentFrame].alloc);
        device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
        swapchain.Recreate(instance.pWindow, doVsync);
        return;
    }

    currentFrame = (currentFrame + 1) % 2;
    {const auto res = device.device.waitForFences(inFlightFences[currentFrame], false, UINT64_MAX);}
    device.device.resetFences(inFlightFences[currentFrame]);
    if (!stageBuffers[currentFrame].isEmpty)
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

    descriptorLayouts.emplace_back(descriptors.descriptorLayout);
    forwardShaders      = MakeTaskMeshShaderObjectsSlang(device.device, "forward", dldid, perspectiveRange, descriptorLayouts);
    forwardPlusShaders  = MakeTaskMeshShaderObjectsSlang(device.device, "forwardPlus", dldid, perspectiveRange, descriptorLayouts);
    forwardPlusTestShaders = MakeTaskMeshShaderObjectsSlang(device.device, "forwardPlusTest", dldid, perspectiveRange, descriptorLayouts);
    lightHeatmapShaders = MakeTaskMeshShaderObjectsSlang(device.device, "lightHeatmap", dldid, perspectiveRange, descriptorLayouts);
    depthprepassShaders = MakeTaskMeshShaderObjectsSlang(device.device, "depthprepass", dldid, perspectiveRange, descriptorLayouts);
    shadowMapShaders = MakeTaskMeshShaderObjectsSlang(device.device, "shadowMapping", dldid, perspectiveRange, descriptorLayouts);

    lightCullingTestShader   = MakeComputeShaderObjectSlang(device.device, "lightCullingTest", dldid, perspectiveRange, descriptorLayouts);
    lightCullingViewShader   = MakeComputeShaderObjectSlang(device.device, "lightCullingView", dldid, perspectiveRange, descriptorLayouts);
    lightCullingShader       = MakeComputeShaderObjectSlang(device.device, "lightCulling", dldid, perspectiveRange, descriptorLayouts);
    screenTileFrustumsShader = MakeComputeShaderObjectSlang(device.device, "screenTileFrustums", dldid, perspectiveRange, descriptorLayouts);

    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
        .setPushConstantRanges(perspectiveRange)
        .setSetLayouts(descriptorLayouts);
    pipelineLayout = device.device.createPipelineLayout(pipelineLayoutInfo);
}
void Renderer::CreateFencesAndSemaphores() {
    auto semaphoreInfo = vk::SemaphoreCreateInfo();
    imageAcquiredSemaphores  [0] = device.device.createSemaphore(semaphoreInfo);
    renderFinishedSemaphores[0] = device.device.createSemaphore(semaphoreInfo);
    imageAcquiredSemaphores  [1] = device.device.createSemaphore(semaphoreInfo);
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

    depthStencilSubresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);
    depthSubresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eDepth)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);

    swapchain = Swapchain(&device.device, device.physicalDevice, instance.surface);
    depthImage = CreateAllocatedImage(device.device, allocator, swapchain.renderExtend, vk::Format::eD24UnormS8Uint, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, nearestSampler);

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
uint32_t Renderer::ParseGLTFImage(const fastgltf::TextureInfo& imageInfo, const fastgltf::Asset& asset, std::vector<AllocatedImage>& txtrs, Uploader& uploader) {
    const auto& texture          = asset.textures[imageInfo.textureIndex];
    const auto& image            = asset.images[texture.imageIndex.value()];
    const auto& sourceBufferView = get<fastgltf::sources::BufferView>(image.data);
    
    const auto& imageBufferView  = asset.bufferViews[sourceBufferView.bufferViewIndex];
    const auto& imageBuffer      = asset.buffers[imageBufferView.bufferIndex];
    const auto& imageData        = get<fastgltf::sources::Array>(imageBuffer.data);

    std::vector<unsigned char> imageChars(imageBufferView.byteLength);
    std::memcpy(imageChars.data(), imageData.bytes.data() + imageBufferView.byteOffset, imageBufferView.byteLength);
    if (sourceBufferView.mimeType == fastgltf::MimeType::JPEG || sourceBufferView.mimeType == fastgltf::MimeType::PNG) {
        int width, height, comp;
        const std::shared_ptr<void> pixels(
            stbi_load_from_memory(imageChars.data(), imageBufferView.byteLength, &width, &height, &comp, STBI_rgb_alpha),
            stbi_image_free
        );

        //txtrs.emplace_back(CreateUploadImage(pixels.get(), vk::Format::eR8G8B8A8Unorm, vk::Extent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }, vk::ImageUsageFlagBits::eSampled));
        txtrs.emplace_back(CreateAllocatedImage(device.device, allocator, width, height, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled, nearestSampler));
        uploader.StageUpload(txtrs[txtrs.size()-1], pixels, width*height*4);
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
    return txtrs.size() - 1;
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
void Renderer::LoadGLTF(const std::filesystem::path& path, Uploader& uploader, glm::mat4 transform) {
    auto total = Timer();
    auto parts = Timer();
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (auto error = data.error(); error != fastgltf::Error::None) {
        std::cout << fastgltf::getErrorMessage(error) << "\n";
        throw;
    }
    std::cout << "Took " << parts.GetMilliseconds() << " ms to open file." << "\n";
    parts.Reset();
    auto gltf = parser.loadGltf(data.get(), path.parent_path());
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
            Light l{};
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
        MaterialIndexGroup matIndices;
        const auto& pbrData = material.pbrData;

        if (pbrData.baseColorTexture.has_value())
            matIndices.diffuse = ParseGLTFImage(pbrData.baseColorTexture.value(), asset, textures, uploader);
        else
            matIndices.diffuse = 0;

        if (pbrData.metallicRoughnessTexture.has_value())
            matIndices.metallicRoughness = ParseGLTFImage(pbrData.metallicRoughnessTexture.value(), asset, textures, uploader);
        else
            matIndices.metallicRoughness = 1;

        if (material.emissiveTexture.has_value())
            matIndices.emissive = ParseGLTFImage(material.emissiveTexture.value(), asset, textures, uploader);
        else
            matIndices.emissive = 1;

        materialIDs.emplace_back(materialIndexGroups.size());
        materialIndexGroups.emplace_back(matIndices);
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
                    const auto pos4 = transform * glm::vec4(positions[i], 1);
                    const auto pos = glm::vec3(pos4.x, pos4.y, pos4.z);
                    verticesLocal[i] = { pos, texCoords[i].x, glm::normalize(normalTransform * normals[i]), texCoords[i].y };
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

void BuildMeshlets(const std::span<uint32_t> indices, const std::span<float> positions,
    std::vector<meshopt_Meshlet>& meshlets, std::vector<uint32_t>& vertices, std::vector<uint8_t>& triangles, std::vector<MeshletBounds>& bounds, const uint32_t meshID) {
    // TODO: Lower max values when mesh has fewer primitives.
    constexpr size_t maxVertices = 64;
    constexpr size_t maxTriangles = 124;
    constexpr float  coneWeight = 0.25f;

    const size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
    meshlets.resize(maxMeshlets);
    vertices.resize(maxMeshlets * maxVertices);
    triangles.resize(maxMeshlets * maxTriangles * 3);

    const size_t vertexCount = positions.size() / 3;
    const size_t meshletCount = meshopt_buildMeshlets(&meshlets[0], &vertices[0], &triangles[0],
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
    const auto prevMeshletsSize = meshlets.size();
    const auto prevMeshletIndicesSize  = meshletTriangles.size();
    const auto prevMeshletVerticesSize = meshletVertices.size();
    const auto prevMeshletBoundsSize   = meshletBounds.size();

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

void Renderer::CreateDebugTextures() {
    const uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    const uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    const uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    std::array<uint32_t, 16 * 16> checkerboardData{};
    for (size_t x = 0; x < 16; x++)
        for (size_t y = 0; y < 16; y++)
            checkerboardData[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
    textures.emplace_back(CreateAllocatedImage(device.device, allocator, vk::Extent2D{ 16, 16 }, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled, nearestSampler));
    textures.emplace_back(CreateAllocatedImage(device.device, allocator, vk::Extent2D{ 1, 1 }, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled, nearestSampler));
    textures.emplace_back(CreateAllocatedImage(device.device, allocator, vk::Extent2D{ 1, 1 }, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled, nearestSampler));

    auto uploader = Uploader(allocator);
    uploader.StageUpload(textures[0], checkerboardData.data(), 16*16*4);
    uploader.StageUpload(textures[1], &black, 4);
    uploader.StageUpload(textures[2], &white, 4);
    const std::function func = [&] {
        stageBuffers[0] = uploader.Upload(graphCompCmdBuffers[0], device.device);
    };
    SubmitImmediate(func);
    vmaDestroyBuffer(allocator, stageBuffers[0].buffer, stageBuffers[0].alloc);
    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);

    materialIndexGroups.emplace_back(0, 1, 1);
}

// Temporary functions.
void Renderer::PushConstant_Draw() {
    SceneInfo sceneInfo{};
    sceneInfo.pointLightCount = pointLights.size();
    sceneInfo.spotLightCount  = spotLights.size();
    sceneInfo.dirLightCount   = dirLights.size();
    sceneInfo.meshCount    = meshViews.size();
    sceneInfo.windowWidth  = swapchain.renderExtend.width;
    sceneInfo.windowHeight = swapchain.renderExtend.height;
    sceneInfo.tileCountX   = (swapchain.renderExtend.width  + swapchain.renderExtend.width  % 16U) / 16U;
    sceneInfo.tileCountY   = (swapchain.renderExtend.height + swapchain.renderExtend.height % 16U) / 16U;

    const auto uploadCamPos = glm::vec4(position, 1);
    const auto invProj = glm::inverse(proj);
    const auto invView = glm::inverse(view);

    auto uploader = Uploader(allocator);
    uploader.StageUpload(viewBuffer, view);
    uploader.StageUpload(invProjBuffer, invProj);
    uploader.StageUpload(invViewBuffer, invView);
    uploader.StageUpload(camPosBuffer, uploadCamPos);
    uploader.StageUpload(sceneInfoBuffer, sceneInfo);
    uploader.StageUpload(projViewBuffer, projViewTransform);

    if (!freezeFrustum) {
        const auto frustumSpan = std::span<Vertex>(vertices).subspan(0, 6);
        uploader.StageUpload(vertexBuffer.buffer, frustumSpan);
    }
    stageBuffers[currentFrame] = uploader.Upload(graphCompCmdBuffers[currentFrame], device.device);

    const PushConstantData pushConstant{
        projViewBuffer.address,
        viewBuffer.address,
        invProjBuffer.address,
        invViewBuffer.address,
        camPosBuffer.address,
        sceneInfoBuffer.address,
        vertexBuffer.address,
        meshletsBuffer.address,
        meshletVerticesBuffer.address,
        meshletTrianglesBuffer.address,
        meshletBoundsBuffer.address,
        meshViewBuffer.address,
        materialBuffer.address,
        lightBuffer.address
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
    if (doLightCulling)
        ImGui::Checkbox("   Show light heatmap", &showLightHeatmap);
    if (doLightCulling)
        ImGui::Checkbox("   Use experimental shader", &useForwardPlusTestShader);
}
void Renderer::LoadModels_Init() {
    auto uploader = Uploader(allocator);
    parser = fastgltf::Parser(fastgltf::Extensions::KHR_lights_punctual);

    //auto helmetTrans = glm::mat4(1.0f);
    //helmetTrans = glm::translate(helmetTrans, glm::vec3(-5.0f, 0, 0));
    //LoadGLTF("assets/DamagedHelmet.glb", uploader, helmetTrans);
    //
    //auto dragonTrans = glm::mat4(1.0f);
    //dragonTrans = glm::translate(dragonTrans, glm::vec3(5.0f, 0, 2.0f));
    //dragonTrans = glm::scale(dragonTrans, glm::vec3(0.1f));
    //LoadGLTF("assets/stanford_dragon.glb", uploader, dragonTrans);
    //
    //auto toyTrans = glm::mat4(1.0f);
    //toyTrans = glm::translate(toyTrans, glm::vec3(-3.0f, 0, 0));
    //toyTrans = glm::rotate<float>(toyTrans, glm::radians(-90.0f), glm::vec3(-1, 0, 0));
    //toyTrans = glm::scale(toyTrans, glm::vec3(0.005f));
    //LoadGLTF("assets/ToyCar.glb", uploader, toyTrans);
    //
    //auto monkeTrans = glm::mat4(1.0f);
    //monkeTrans = glm::translate(monkeTrans, glm::vec3(-2, -4, 3));
    //LoadGLTF("assets/monke.glb", uploader, monkeTrans);
    //
    //Many sponzas for benchmarking.
    for (size_t i = 0; i < 1; i++) {
        for (size_t j = 0; j < 1; j++) {
            for (size_t k = 0; k < /*3*/1; k++) {
                auto sponzaTrans = glm::mat4(1.0f);
                sponzaTrans = glm::translate(sponzaTrans, glm::vec3(i * 40, j * 20, k * 25));
                sponzaTrans = glm::scale(sponzaTrans, glm::vec3(0.01f));
                //LoadGLTF("assets/sponza.glb", uploader, sponzaTrans);
            }
        }
    }

    auto bistroTrans = glm::mat4(1.0f);
    bistroTrans = glm::scale(bistroTrans, glm::vec3(0.001f));
    LoadGLTF("assets/Buggy.glb", uploader, bistroTrans);

    const std::function upload = [&] {
        stageBuffers[0] = uploader.Upload(graphCompCmdBuffers[0], device.device);
    };
    SubmitImmediate(upload);
    vmaDestroyBuffer(allocator, stageBuffers[0].buffer, stageBuffers[0].alloc);

    std::cout << "\nLoaded all models.\n";
    std::cout << "Size of all vertices: " << sizeof(Vertex) * vertices.size() << " Bytes\n";
}
void Renderer::SpawnLights_Init() {
    // xyz: 20 0 25 "Centre"
    constexpr auto centre = glm::vec3(20, 0, 25);
    std::random_device randomDevice;
    auto ranGen = std::mt19937(3529725061);

    std::uniform_int_distribution<int> posxzDistrib(-10, 60);
    std::uniform_int_distribution<int> posyDistrib(-10, 20);
    std::uniform_int_distribution<int> rangeDistrib(5, 30);
    std::uniform_real_distribution<float> colorDistrib(0, 1);
    for (size_t i = 0; i < 0; i++) {
        const auto pos = glm::vec3(posxzDistrib(ranGen), posyDistrib(ranGen), posxzDistrib(ranGen));
        const auto dir = centre - pos;

        Light sl{};
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
    for (size_t i = 0; i < 0; i++) {
        const auto pos = glm::vec3(posxzDistrib(ranGen), posyDistrib(ranGen), posxzDistrib(ranGen));

        Light pl{};
        pl.pos = pos;
        pl.radius = rangeDistrib(ranGen);
        pl.color = glm::vec3(colorDistrib(ranGen), colorDistrib(ranGen), colorDistrib(ranGen));
        pl.falloff = 0.0f;
        pl.lightType = 0;
        pointLights.emplace_back(pl);
    }
    Light pl{};
    pl.pos     = glm::vec3(20.0f, 0.0f, 0.0f);
    pl.radius  = 25.0f;
    pl.color   = glm::vec3(0.0f, 0.2f, 0.5f);
    pl.falloff = 10.0f;
    pl.lightType = 0;
    pointLights.emplace_back(pl);

    Light dl{};
    dl.color     = glm::vec3(0.95f, 0.95f, 0.6f);
    dl.lightDir  = glm::vec4(-1.0f, -1.0f, -1.0f, 1);
    dl.lightType = 2;
    dirLights.emplace_back(dl);

    lights.resize(pointLights.size() + spotLights.size() + dirLights.size());
    size_t lightMemIndex = 0;
    if(!pointLights.empty())
        std::memcpy(&lights[lightMemIndex], pointLights.data(), pointLights.size() * sizeof(Light));
    lightMemIndex += pointLights.size();
    if (!spotLights.empty())
        std::memcpy(&lights[lightMemIndex], spotLights.data(), spotLights.size() * sizeof(Light));
    lightMemIndex += spotLights.size();
    if (!dirLights.empty())
        std::memcpy(&lights[lightMemIndex], dirLights.data(), dirLights.size() * sizeof(Light));
}
void Renderer::CreateSamplers_Init() {
    constexpr auto nearestSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest);
    constexpr auto linearSamplerInfo = vk::SamplerCreateInfo()
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear);

    nearestSampler = device.device.createSampler(nearestSamplerInfo);
    linearSampler = device.device.createSampler(linearSamplerInfo);
}
void Renderer::CreateDescSets_Init() {
    constexpr auto usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

    // Buffers.
    lightIndicesSize = MAX_LIGHTS_PER_TILE * sizeof(int) * std::ceil(swapchain.renderExtend.height / 16) * std::ceil(swapchain.renderExtend.width / 16);
    lightIndicesBuffer  = CreateAllocatedBuffer(device.device, allocator, lightIndicesSize, usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    lightIndicesViewSize = (lights.size() + 2U) * sizeof(int);
    lightIndicesViewBuffer = CreateAllocatedBuffer(device.device, allocator, lightIndicesViewSize, usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    tileFrustumsSize = sizeof(glm::vec4) * 6U * (std::ceil(swapchain.renderExtend.height / 16) * std::ceil(swapchain.renderExtend.width / 16) + 1);
    tileFrustumBuffer = CreateAllocatedBuffer(device.device, allocator, tileFrustumsSize, usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    // Upload buffer addresses.
    bufferAddresses.meshletsAddress = meshletsBuffer.address;
    bufferAddresses.meshletVerticesAddress = meshletVerticesBuffer.address;
    bufferAddresses.meshletTrianglesAddress = meshletTrianglesBuffer.address;
    bufferAddresses.meshletBoundsAddress = meshletBoundsBuffer.address;

    bufferAddresses.meshViewBufferAddress = meshViewBuffer.address;
    bufferAddresses.vertexBufferAddress = vertexBuffer.address;
    bufferAddresses.materialBufferAddress = materialBuffer.address;
    bufferAddresses.lightBufferAddress = lightBuffer.address;

    addressBuffer.buffer = CreateAllocatedBuffer(device.device, allocator, lightIndicesSize, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE).buffer;

    meshletBoundsBuffer    = CreateAllocatedBuffer(device.device, allocator, sizeof(MeshletBounds) * meshletBounds.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    meshletsBuffer         = CreateAllocatedBuffer(device.device, allocator, sizeof(meshopt_Meshlet) * meshlets.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    meshletVerticesBuffer  = CreateAllocatedBuffer(device.device, allocator, sizeof(uint32_t) * meshletVertices.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    meshletTrianglesBuffer = CreateAllocatedBuffer(device.device, allocator, sizeof(uint8_t) * meshletTriangles.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    lightBuffer      = CreateAllocatedBuffer(device.device, allocator, sizeof(Light) * lights.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    materialBuffer   = CreateAllocatedBuffer(device.device, allocator, sizeof(MaterialIndexGroup) * materialIndexGroups.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    meshViewBuffer   = CreateAllocatedBuffer(device.device, allocator, sizeof(MeshView) * meshViews.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);
    vertexBuffer     = CreateAllocatedBuffer(device.device, allocator, sizeof(Vertex) * vertices.size(), usage, VMA_MEMORY_USAGE_GPU_ONLY);

    dirShadowTransBuffer = CreateAllocatedBuffer(device.device, allocator, sizeof(glm::mat4), vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    projViewBuffer  = CreateAllocatedBuffer(device.device, allocator, sizeof(glm::mat4), usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    viewBuffer      = CreateAllocatedBuffer(device.device, allocator, sizeof(glm::mat4), usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    invProjBuffer   = CreateAllocatedBuffer(device.device, allocator, sizeof(glm::mat4), usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    invViewBuffer   = CreateAllocatedBuffer(device.device, allocator, sizeof(glm::mat4), usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    camPosBuffer    = CreateAllocatedBuffer(device.device, allocator, sizeof(glm::mat4), usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    sceneInfoBuffer = CreateAllocatedBuffer(device.device, allocator, sizeof(SceneInfo), usage, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    constexpr float near  = 0.01f;
    constexpr float far   = 4000.0f;
    const auto dirOrtho = glm::ortho(-35.0f, 35.0f, -35.0f, 35.0f, far, near);
    const auto dirLightView = glm::lookAt(20.0f * glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    dirShadowTrans = dirOrtho * dirLightView;

    auto uploader = Uploader(allocator);
    uploader.StageUpload(addressBuffer, bufferAddresses);
    uploader.StageUpload(vertexBuffer, vertices);
    uploader.StageUpload(meshViewBuffer, meshViews);
    uploader.StageUpload(meshletBoundsBuffer, meshletBounds);
    uploader.StageUpload(meshletsBuffer, meshlets);
    uploader.StageUpload(meshletVerticesBuffer, meshletVertices);
    uploader.StageUpload(meshletTrianglesBuffer, meshletTriangles);
    uploader.StageUpload(materialBuffer, materialIndexGroups);
    uploader.StageUpload(lightBuffer, lights);
    uploader.StageUpload(dirShadowTransBuffer, dirShadowTrans);

    const std::function upload = [&] {
        stageBuffers[0] = uploader.Upload(graphCompCmdBuffers[0], device.device);
    };
    SubmitImmediate(upload);
    vmaDestroyBuffer(allocator, stageBuffers[0].buffer, stageBuffers[0].alloc);

    depthImage.sampler = nearestSampler;

    shadowMapImage = CreateAllocatedImage(device.device, allocator, SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, nearestSampler);

    descriptors = Descriptors();
    descriptors.AddImageDescriptor(textures, vk::ImageLayout::eGeneral, 0);
    descriptors.AddImageDescriptor(depthImage, vk::ImageLayout::eDepthReadOnlyOptimal, 1);
    descriptors.AddImageDescriptor(shadowMapImage, vk::ImageLayout::eDepthReadOnlyOptimal, 5);
    descriptors.AddBufferDescriptor(lightIndicesBuffer.buffer, lightIndicesSize, vk::DescriptorType::eStorageBuffer, 2);
    descriptors.AddBufferDescriptor(tileFrustumBuffer.buffer, tileFrustumsSize, vk::DescriptorType::eStorageBuffer, 4);
    descriptors.AddBufferDescriptor(dirShadowTransBuffer.buffer, sizeof(glm::mat4), vk::DescriptorType::eUniformBuffer, 6);
    descriptors.AddBufferDescriptor(lightIndicesViewBuffer.buffer, lightIndicesViewSize, vk::DescriptorType::eStorageBuffer, 7);

    const std::function descFunc = [&] { descriptors.CreateSetsAndWriteDescriptors(device.device); };
    SubmitImmediate(descFunc);
    device.device.resetCommandPool(graphicsComputeCommand.cmdPool);
}