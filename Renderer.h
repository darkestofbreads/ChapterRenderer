#pragma once
#include "Shader.h"
#include "Swapchain.h"
#include "Device.h"
#include "Instance.h"
#include "Command.h"
#include "Timer.h"

#include "stb_image.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_vulkan.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SWIZZLE

#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <meshoptimizer.h>

#include <iostream>
#include <vector>
#include <random>

struct Vertex {
	glm::vec3 Position;
	float U;
	glm::vec3 Normal;
	float V;
};
struct MaterialIndexGroup {
	uint32_t diffuse;
	uint32_t metallicRoughness;
	uint32_t emissive;
};
struct PointLight {
	glm::vec3 Position;
	float radius;
	glm::vec3 color;
	float falloff;
};
struct DirLight {
	glm::vec4 lightDir;
	glm::vec4 color;
};
struct SpotLight {
	glm::vec3 pos;
	float radius;
	glm::vec4 lightDir;
	glm::vec3 color;
	float falloff;
	float cutoff;
	float innerCutoff;
	float fillerA;
	float fillerB;
};
struct MeshView {
	uint32_t start;
	uint32_t end;
	uint32_t material;
	uint32_t filler;
};
struct SceneInfo {
	uint32_t meshCount;
	uint32_t pointLightCount;
	uint32_t spotLightCount;
	uint32_t directionLightCount;
};
struct PushConstantData {
	glm::mat4 projView;
	glm::mat4 worldTransform;
	SceneInfo sceneInfo;

	vk::DeviceAddress meshletsAddress;
	vk::DeviceAddress meshletVerticesAddress;
	vk::DeviceAddress meshletTrianglesAddress;

	vk::DeviceAddress meshViewBufferAddress;
	vk::DeviceAddress vertexBufferAddress;
	vk::DeviceAddress materialBufferAddress;

	vk::DeviceAddress pointLightBufferAddress;
	vk::DeviceAddress spotLightBufferAddress;
	vk::DeviceAddress dirLightBufferAddress;
};
struct AllocatedBuffer {
	vk::Buffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo info;
};
struct AllocatedImage {
	vk::Image image;
	vk::ImageView view;
	VmaAllocation alloc;
};
struct GPUBuffer {
	AllocatedBuffer buffer;
	vk::DeviceAddress bufferAddress;
};
struct Chunk {
	uint32_t blocks[32][32];
	uint32_t x, y;
};


class Renderer
{
public:
	Renderer(SDL_Window* window, std::atomic<bool>* ready);
	void Draw();

	void Move(float forward, float sideward);
	void Teleport(glm::vec3 pos, glm::vec3 direction = glm::vec3(0, 0, 1));
	float yaw = 0;
	float pitch = 0;
private:
	// Temporary abstractions.
	void PushConstant_Draw();
	void ImGui_Draw(double frameTime);
	void LoadModels_Init();
	void SpawnLights_Init();
	void UploadAll_Init();
	void CreateSamplers_Init();
	void CreateDescSets_Init();
	void OptimizeMesh();

	void SubmitAndPresent(uint32_t imageIndex);
	void SubmitImmediate(const std::function<void()>& func);
	void BeginRendering(const uint32_t imageIndex);
	bool AquireImageIndex(uint32_t& index);
	bool doVsync = true;
	bool requestNewSwapchain = false;

	void BuildGlobalTransform();
	void InitImGui(SDL_Window* window);
	void CreatePipeline();
	void CreateFencesAndSemaphores();
	void InitMainObjects(SDL_Window* window, std::atomic<bool>* ready);

	GPUBuffer UploadMesh(std::span<Vertex> vertices);
	uint32_t ParseGLTFImage(const fastgltf::TextureInfo& imageInfo, const fastgltf::Asset& asset, std::vector<AllocatedImage>& textures);

	AllocatedImage CreateDepthImage();
	AllocatedImage CreateImage(vk::Format format, vk::Extent2D extend, vk::ImageUsageFlags usage, vk::ImageSubresourceRange subresource, bool makeMipmaps = false);
	AllocatedImage CreateUploadImage(void* data, vk::Format format, vk::Extent2D extend, vk::ImageUsageFlags usage, bool makeMipmaps = false);
	vk::ImageView  CreateImageView(const vk::Image& image, const vk::Format& format, const vk::ImageSubresourceRange& subresource);

	// Textures.
	void CreateDebugTextures();
	std::vector<vk::DescriptorSet> imageDescSet;
	std::vector<AllocatedImage> textures;
	vk::DescriptorSetLayout imageDescLayout;
	vk::Sampler nearestSampler;
	vk::Sampler linearSampler;

	void LoadGLTF(std::filesystem::path path, glm::mat4 transform = glm::mat4(1.0f));
	fastgltf::Parser parser;

	GPUBuffer meshBuffer;
	AllocatedBuffer CreateBuffer(size_t allocSize, vk::Flags<vk::BufferUsageFlagBits> usage, VmaMemoryUsage memUsage);
	VmaAllocator allocator;

	template<typename T>
	vk::DeviceAddress UploadData(std::span<T> data);

	vk::DeviceAddress meshletsAddress;
	vk::DeviceAddress meshletVerticesAddress;
	vk::DeviceAddress meshletTrianglesAddress;

	vk::DeviceAddress meshViewBufferAddress;
	vk::DeviceAddress materialBufferAddress;
	vk::DeviceAddress pointLightBufferAddress;
	vk::DeviceAddress spotLightBufferAddress;
	vk::DeviceAddress dirLightBufferAddress;
	glm::mat4 vertexTransform;
	glm::mat4 worldTransform;
	glm::vec3 position  = glm::vec3(0);
	glm::vec3 direction = glm::vec3(0, 0, 1.0f);

	ImVec4 clearColorUI;

	Device device;
	Swapchain swapchain;
	std::array<AllocatedImage, IMAGE_COUNT> depthImages;
	vk::ImageSubresourceRange depthSubresourceRange;

	Instance instance;
	Timer frameTimer;

	Command command;
	vk::Queue graphicsQueue;
	vk::PipelineLayout pipelineLayout;
	vk::detail::DispatchLoaderDynamic dldid;

	uint32_t currentFrame = 0;
	std::array<vk::CommandBuffer, 2> cmdBuffers;
	std::array <vk::Semaphore, 2> imageAquiredSemaphores;
	std::array <vk::Semaphore, 2> renderFinishedSemaphores;
	std::array <vk::Fence, 2> inFlightFences;
	vk::Fence immediateFence;

	std::vector<meshopt_Meshlet>	meshlets;
	std::vector<uint32_t>			meshletVertices;
	std::vector<uint8_t>			meshletTriangles;
	std::vector<MeshView>			meshViews;
	std::vector<Vertex>				vertices;
	std::vector<MaterialIndexGroup> materialIndexGroups;
	std::vector<uint32_t>			materialIndices;

	std::vector<uint32_t>			indices;

	std::vector<PointLight>			pointLights;
	std::vector<SpotLight>			spotLights;
	std::vector<DirLight>			dirLights;

	std::vector<vk::ShaderEXT> shaders;

	std::array<vk::ShaderStageFlagBits, 4> meshStages = {
	vk::ShaderStageFlagBits::eVertex,
	vk::ShaderStageFlagBits::eTaskEXT,
	vk::ShaderStageFlagBits::eMeshEXT,
	vk::ShaderStageFlagBits::eFragment
	};
};