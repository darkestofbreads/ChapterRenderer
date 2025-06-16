#define VMA_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include "Renderer.h"

Renderer::Renderer(SDL_Window* window, std::atomic<bool>* ready) {
    InitMainObjects(window, ready);
    CreateFencesAndSemaphores();

    CreateSamplers_Init();
    CreateDebugTextures();

    LoadModels_Init();
    SpawnLights_Init();
    OptimizeMesh();
    UploadAll_Init();

    CreateDescSets_Init();
    CreatePipeline();

    // Setup UI.
    InitImGui(window);
}

void Renderer::Draw() {
    command.SetCurrentFrame(currentFrame);

    double frameTime = frameTimer.GetMilliseconds();
    frameTimer.Reset();
    uint32_t imageIndex;
    if (!AquireImageIndex(imageIndex)) return;

    ImGui_Draw(frameTime);
    BeginRendering(imageIndex);
    PushConstant_Draw();
    cmdBuffers[currentFrame].bindShadersEXT(meshStages, shaders, dldid);
    // Launch one invocation per meshlet,
    // then inside each invocation, emit one mesh shader each primitive.
    // Draw meshes.
    cmdBuffers[currentFrame].drawMeshTasksEXT(meshlets.size(), 1, 1, dldid);
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmdBuffers[currentFrame]));

    SubmitAndPresent(imageIndex);
}

// Camera related functions.
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

    worldTransform = glm::lookAt(position, position + direction, glm::vec3(0, 1.0f, 0));
    vertexTransform = {
        glm::perspective(glm::radians(90.0f), (float)swapchain.renderExtend.width / (float)swapchain.renderExtend.height, 0.1f, 4000.0f) *
        worldTransform
    };
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
void Renderer::BeginRendering(const uint32_t imageIndex) {
    auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuffers[currentFrame].begin(beginInfo);

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
    cmdBuffers[currentFrame].setViewportWithCount(viewport);

    auto scissor = vk::Rect2D()
        .setExtent(swapchain.renderExtend)
        .setOffset({ 0 ,0 });
    cmdBuffers[currentFrame].setScissorWithCount(scissor);

    auto colorAttachment = vk::RenderingAttachmentInfo()
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue({ 0.1f, 0.1f, 0.3f, 1.0f }))
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setImageView(swapchain.imageViews[imageIndex])
        .setResolveMode(vk::ResolveModeFlagBits::eNone);

    cmdBuffers[currentFrame].setDepthTestEnable(vk::True);
    cmdBuffers[currentFrame].setDepthWriteEnable(vk::True);
    cmdBuffers[currentFrame].setDepthCompareOp(vk::CompareOp::eLessOrEqual);

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
    cmdBuffers[currentFrame].beginRendering(renderInfo);
}
void Renderer::SubmitImmediate(const std::function<void()>& func) {
    device.device.resetFences(immediateFence);

    vk::CommandBufferBeginInfo beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuffers[0].begin(beginInfo);
    cmdBuffers[1].begin(beginInfo);

    func();

    cmdBuffers[0].end();
    cmdBuffers[1].end();

    vk::SubmitInfo submitInfo = vk::SubmitInfo()
        .setCommandBuffers(cmdBuffers);
    graphicsQueue.submit(submitInfo, immediateFence);
    device.device.waitForFences(immediateFence, false, UINT64_MAX);
}
void Renderer::SubmitAndPresent(uint32_t imageIndex) {
    // End rendering.
    cmdBuffers[currentFrame].endRendering();
    command.TransitionImage(swapchain.images[imageIndex], swapchain.subresourceRange, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eNone);
    command.TransitionImage(depthImages[imageIndex].image, depthSubresourceRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eNone);
    cmdBuffers[currentFrame].end();
    // Submit work.
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo = vk::SubmitInfo()
        .setCommandBuffers(cmdBuffers[currentFrame])
        .setWaitSemaphores(imageAquiredSemaphores[currentFrame])
        .setSignalSemaphores(renderFinishedSemaphores[currentFrame])
        .setWaitDstStageMask(waitStage);
    graphicsQueue.submit(submitInfo, inFlightFences[currentFrame]);

    // Present image.
    vk::PresentInfoKHR info = vk::PresentInfoKHR()
        .setSwapchains(swapchain.swapchain)
        .setImageIndices(imageIndex)
        .setWaitSemaphores(renderFinishedSemaphores[currentFrame]);
    try {
        graphicsQueue.presentKHR(info);
    }
    catch (std::exception e) {
        requestNewSwapchain = true;
    }
    if (requestNewSwapchain) {
        requestNewSwapchain = false;
        device.device.waitForFences(inFlightFences[currentFrame], false, UINT64_MAX);
        device.device.resetFences  (inFlightFences[currentFrame]);
        device.device.resetCommandPool(command.cmdPool);
        swapchain.Recreate(instance.pWindow, doVsync);
        return;
    }
    currentFrame = (currentFrame + 1) % 2;
    device.device.waitForFences(inFlightFences[currentFrame], false, UINT64_MAX);
    device.device.resetFences(inFlightFences[currentFrame]);
    cmdBuffers[currentFrame].reset();
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
void Renderer::CreatePipeline() {
    auto perspectiveRange = vk::PushConstantRange()
        .setOffset(0)
        .setSize(sizeof(PushConstantData))
        .setStageFlags(vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment);
    // Shader object.
    shaders = MakeTaskMeshShaderObjects(device.device, "shaders/triangle.task.spv", "shaders/triangle.mesh.spv", "shaders/fragment.frag.spv", dldid, perspectiveRange, imageDescLayout);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
        .setPushConstantRanges(perspectiveRange)
        .setSetLayouts(imageDescLayout);
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
        .setAspectMask(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
        .setBaseMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1)
        .setLevelCount(1);

    swapchain = Swapchain(&device.device, device.physicalDevice, instance.surface);
    for (auto& i : depthImages)
        i = CreateDepthImage();

    graphicsQueue = device.device.getQueue(device.graphicsQueueFamilyIndex, 0);
    command = Command(device);
    cmdBuffers[0] = command.cmdBuffer[0];
    cmdBuffers[1] = command.cmdBuffer[1];
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
    std::cout << "Took " << parts.GetMilliseconds() << " ms to load materials." << "\n";
    parts.Reset();
    // Load meshes.
    for (const auto& mesh : asset.meshes) {
        MeshView meshView;
        meshView.start = indices.size();
        for (const auto& primitive : mesh.primitives) {
            size_t prevVertexSize = vertices.size();
            size_t prevIndexSize  = indices.size();

            auto positions        = ReadAttribute<glm::vec3>(asset, primitive, "POSITION");
            auto normals          = ReadAttribute<glm::vec3>(asset, primitive, "NORMAL");
            const auto& texCoords = ReadAttribute<glm::vec2>(asset, primitive, "TEXCOORD_0");

            std::cout << "Took " << parts.GetMilliseconds() << " ms to read attributes." << "\n";
            parts.Reset();

            for (size_t t = 0; t < positions.size(); t++) {
                auto pos = transform * glm::vec4(positions[t], 1);
                positions[t] = pos.xyz;
                normals[t]   = normalTransform * normals[t];
            }

            std::cout << "Took " << parts.GetMilliseconds() << " ms to transform normals and positions." << "\n";
            parts.Reset();

            // Add vertices to pool.
            size_t vertOffset = vertices.size();
            vertices.resize(vertOffset + positions.size());
            for (size_t i = 0; i < positions.size(); i++)
                vertices[i + vertOffset] = { positions[i], texCoords[i].x, normals[i], texCoords[i].y };

            std::cout << "Took " << parts.GetMilliseconds() << " ms to add vertices." << "\n";
            parts.Reset();

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

            size_t indexCount = indAcr.count;
            indices.resize(indexCount + indices.size());

            size_t count = 0;
            // If possible, the GLTF will use uint16 to reduce file size.
            if (indAcr.componentType == fastgltf::ComponentType::UnsignedShort) {
                std::vector<uint16_t> rawIndices(indAcr.count);
                std::memcpy(rawIndices.data(), indData.bytes.data() + indAcr.byteOffset + indBufferView.byteOffset, sizeof(uint16_t) * rawIndices.size());

                for (size_t i = 0; i < indexCount; i++) {
                    indices[prevIndexSize + i] = prevVertexSize + static_cast<uint32_t>(rawIndices[i]);
                }
            }
            else {
                std::vector<uint32_t> rawIndices(indAcr.count);
                std::memcpy(rawIndices.data(), indData.bytes.data() + indAcr.byteOffset + indBufferView.byteOffset, sizeof(uint32_t) * rawIndices.size());

                // TODO: Do memcpy instead.
                for (size_t i = 0; i < indexCount; i++) {
                    indices[prevIndexSize + i] = prevVertexSize + rawIndices[i];
                }
            }
            meshView.end = indices.size() - 1;
            meshView.filler = 0;
            meshView.material = 0;
            meshViews.emplace_back(meshView);

            std::cout << "Took " << parts.GetMilliseconds() << " ms to format and add indices." << "\n";
        }
    }
    std::cout << "Took " << total.GetMilliseconds() << " ms to fully load model." << "\n\n";
}
void Renderer::OptimizeMesh() {
    {
        // Indexing.
        Timer timer = Timer();
        std::vector<uint32_t> remap(indices.size());
        std::vector<uint32_t> newIndices(indices.size());

        size_t oldVertCount = vertices.size();
        size_t vertCount    = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), oldVertCount, sizeof(Vertex));
        std::vector<Vertex> newVertices(vertCount);

        meshopt_remapIndexBuffer (newIndices.data(), indices.data(), indices.size(), remap.data());
        meshopt_remapVertexBuffer(newVertices.data(), vertices.data(), oldVertCount, sizeof(Vertex), remap.data());
        std::cout << "Reduced vertex count by " << oldVertCount - newVertices.size() << " in " << timer.GetMilliseconds() << " ms" << "\n";
        vertices = newVertices;
        indices  = newIndices;
    }
    {
        // Vertex cache optimization. (Questionable, seems to degrade performance)
        Timer timer = Timer();
        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
        std::cout << "Reordered indices in " << timer.GetMilliseconds() << " ms" << "\n";
    }
    std::vector<float> positions(vertices.size() * 3);
    for (size_t i = 0; i < vertices.size(); i++) {
        positions[i] = vertices[i].Position.x;
        positions[i + 1] = vertices[i].Position.y;
        positions[i + 2] = vertices[i].Position.z;
    }
    {
        // Overdraw optimization.
        Timer timer = Timer();
        meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), positions.data(), vertices.size(), sizeof(float) * 3, 1.05f);
        std::cout << "Optimized overdraw in " << timer.GetMilliseconds() << " ms" << "\n";
    }
    {
        // Vertex fetch optimization.
        Timer timer = Timer();
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));
        std::cout << "Reordered vertices in " << timer.GetMilliseconds() << " ms" << "\n";
    }
    {
        // Build and optimize meshlets.
        Timer timer = Timer();
        const size_t maxVertices  = 64;
        const size_t maxTriangles = 124;
        const float  coneWeight   = 0.25f;
        size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
        meshlets         = std::vector<meshopt_Meshlet>(maxMeshlets);
        meshletVertices  = std::vector<uint32_t>(maxMeshlets * maxVertices);
        meshletTriangles = std::vector<uint8_t>(maxMeshlets * maxTriangles * 3);

        size_t meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(),
            indices.size(), positions.data(), vertices.size(), sizeof(float) * 3, maxVertices, maxTriangles, coneWeight);

        const meshopt_Meshlet& lastElement = meshlets[meshletCount - 1];
        meshletVertices.resize(lastElement.vertex_offset + lastElement.vertex_count);
        meshletTriangles.resize(lastElement.triangle_offset + ((lastElement.triangle_count * 3 + 3) & ~3));
        std::cout << "Built meshlets in " << timer.GetMilliseconds() << " ms" << "\n";
        timer.Reset();
        //meshopt_optimizeMeshlet(meshletVertices.data(), meshletTriangles.data(), lastElement.triangle_count, lastElement.vertex_count);
        std::cout << "Optimized meshlets in " << timer.GetMilliseconds() << " ms" << "\n";
        timer.Reset();
        
        meshletsAddress         = UploadData<meshopt_Meshlet>(meshlets);
        meshletVerticesAddress  = UploadData<uint32_t>(meshletVertices);
        meshletTrianglesAddress = UploadData<uint8_t>(meshletTriangles);
        std::cout << "Uploaded meshlets in " << timer.GetMilliseconds() << " ms" << "\n";
    }
    {
        // Shadow indexing.
    }
    {
        // Vertex quantization.
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
        cmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, buffer.buffer, region);
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
GPUMeshBuffer Renderer::UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
    // GPU only buffers.
    const size_t vertSize = vertices.size() * sizeof(Vertex);
    const size_t indiSize =  indices.size() * sizeof(uint32_t);

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
        cmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, meshbuffer.vertexBuffer.buffer, vertRegion);

        auto indexRegion = vk::BufferCopy()
            .setSrcOffset(vertSize)
            .setSize(indiSize);
        cmdBuffers[currentFrame].copyBuffer(stageBuffer.buffer, meshbuffer.indexBuffer.buffer, indexRegion);
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

        command.cmdBuffer[currentFrame].copyBufferToImage(upload.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, imageCopy);
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
    BuildGlobalTransform();
    SceneInfo sceneInfo;
    sceneInfo.pointLightCount     = pointLights.size();
    sceneInfo.spotLightCount      = spotLights.size();
    sceneInfo.directionLightCount = dirLights.size();
    sceneInfo.meshCount           = meshViews.size();

    PushConstantData pushConstant{
        vertexTransform,
        worldTransform,
        sceneInfo,

        meshletsAddress,
        meshletVerticesAddress,
        meshletTrianglesAddress,

        meshViewBufferAddress,
        meshBuffer.vertexBufferAddress,
        meshBuffer.indexBufferAddress,
        materialBufferAddress,

        pointLightBufferAddress,
        spotLightBufferAddress,
        dirLightBufferAddress
    };
    cmdBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, imageDescSet, nullptr);
    cmdBuffers[currentFrame].pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstantData), &pushConstant);
}
void Renderer::ImGui_Draw(double frameTime) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    std::string positionStr = "X: " + std::to_string(position.x) + " Y: " + std::to_string(position.y) + " Z: " + std::to_string(position.z) + "\n";
    std::string frameTimeStr = std::to_string(frameTime) + " ms | " + std::to_string(1000 / frameTime) + " fps\n";
    ImGui::Text(positionStr.c_str());
    ImGui::Text(frameTimeStr.c_str());
    requestNewSwapchain = ImGui::Checkbox("Toggle Vsync", &doVsync);
    if (requestNewSwapchain)
        std::cout << "Checkbox pressed!\n";
}
void Renderer::LoadModels_Init() {
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

    // Many sponzas for benchmarking.
    //for (size_t i = 0; i < 2; i++) {
    //    for (size_t j = 0; j < 2; j++) {
    //        for (size_t k = 0; k < /*3*/1; k++) {
    //            auto sponzaTrans = glm::mat4(1.0f);
    //            sponzaTrans = glm::translate(sponzaTrans, glm::vec3(i * 40, j * 20, k * 25));
    //            sponzaTrans = glm::rotate<float>(sponzaTrans, glm::radians(180.0f), glm::vec3(-1, 0, 0));
    //            sponzaTrans = glm::scale(sponzaTrans, glm::vec3(0.01f));
    //            LoadGLTF("assets/sponza.glb", sponzaTrans);
    //        }
    //    }
    //}
    std::cout << "\nLoaded all models.\n";
    std::cout << "Size of all vertices: " << sizeof(Vertex) * vertices.size() << " Bytes, indices: " << sizeof(glm::uvec4) * indices.size() << " Bytes\n";
}
void Renderer::SpawnLights_Init() {
    // xyz: 20 0 25 "Centre"
    const auto centre = glm::vec3(20, 0, 25);
    std::random_device randomDevice;
    auto ranGen = std::mt19937(3529725061);

    std::uniform_int_distribution<int> posxzDistrib(-10, 60);
    std::uniform_int_distribution<int> posyDistrib(-10, 20);
    std::uniform_int_distribution<int> rangeDistrib(5, 30);
    for (size_t i = 0; i < 100; i++) {
        const auto pos = glm::vec3(posxzDistrib(ranGen), posyDistrib(ranGen), posxzDistrib(ranGen));
        const auto dir = centre - pos;
        spotLights.emplace_back(pos, rangeDistrib(ranGen),
            glm::vec4(glm::normalize(dir), 1),
            glm::vec3(1),
            0.0f, 0.95f, 0.96f);
    }
    pointLights.emplace_back(glm::vec3(20.0f, 0.0f, 0.0f), 25.0f, glm::vec3(0.0f, 0.2f, 0.5f), 10.0f);
    dirLights.emplace_back(glm::vec4(-1.0f, 1.0f, -1.0f, 1), glm::vec4(0.85f, 0.85f, 0.5f, 1));
    spotLights.emplace_back(glm::vec3(-9.0f, -1.0f, 2.0f), 10.0f, glm::vec4(1.0f, 0.0f, -1.0f, 1), glm::vec3(1), 0.0f, 0.95f, 0.96f);
}
void Renderer::UploadAll_Init() {
    // Upload geometry and material indices.
    if (indices.size() > 0 && vertices.size() > 0)
        meshBuffer = UploadMesh(indices, vertices);
    if (meshViews.size() > 0)
        meshViewBufferAddress = UploadData<MeshView>(meshViews);

    // Upload materials.
    if (materialIndexGroups.size() > 0)
        materialBufferAddress   = UploadData<MaterialIndexGroup>(materialIndexGroups);

    // Upload lights.
    if (pointLights.size() > 0)
        pointLightBufferAddress = UploadData<PointLight>(pointLights);
    if (spotLights.size() > 0)
        spotLightBufferAddress  = UploadData<SpotLight>(spotLights);
    if (dirLights.size() > 0)
        dirLightBufferAddress   = UploadData<DirLight>(dirLights);
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
    // Set bindings for the push descriptor (textures are on set = 0, binding = 0).
    auto layoutBinding = vk::DescriptorSetLayoutBinding()
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(textures.size())
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    auto descriptorLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
        .setBindings(layoutBinding);
    imageDescLayout = device.device.createDescriptorSetLayout(descriptorLayoutInfo);

    // Descriptor pool.
    auto imagePoolSize = vk::DescriptorPoolSize()
        .setType(vk::DescriptorType::eCombinedImageSampler)
        // Change to frames in flight count.
        .setDescriptorCount(textures.size());
    auto imagePoolInfo = vk::DescriptorPoolCreateInfo()
        // Change to frames in flight count.
        .setMaxSets(1)
        .setPoolSizes(imagePoolSize);
    auto imagePool = device.device.createDescriptorPool(imagePoolInfo);

    // Descriptor set.
    auto imageDescAlloc = vk::DescriptorSetAllocateInfo()
        .setDescriptorPool(imagePool)
        .setSetLayouts(imageDescLayout);
    imageDescSet = device.device.allocateDescriptorSets(imageDescAlloc);

    // Write to descriptors.
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
        .setDstSet(imageDescSet[0])
        .setDstBinding(0)
        .setDescriptorCount(imageDescriptors.size())
        .setImageInfo(imageDescriptors);
    std::array<vk::WriteDescriptorSet, 1> descWrites{
        descWrite
    };

    std::function<void()> descFunc = [&]() { device.device.updateDescriptorSets(descWrites, nullptr); };
    SubmitImmediate(descFunc);
    device.device.resetCommandPool(command.cmdPool);
}