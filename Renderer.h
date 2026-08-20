#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SWIZZLE

#include "Shader.h"
#include "Swapchain.h"
#include "Device.h"
#include "Instance.h"
#include "Command.h"
#include "Timer.h"
#include "Uploader.h"
#include "Descriptors.h"
#include "GLTF.h"

#include "stb_image.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_vulkan.h"

#include <vulkan/vulkan.hpp>

#include <vma/vk_mem_alloc.h>

#include <glm/common.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <meshoptimizer.h>

#include <iostream>
#include <vector>
#include <random>

constexpr uint32_t TILE_SIZE = 16;
constexpr uint32_t MAX_LIGHTS_PER_TILE = 128;
constexpr uint32_t SHADOW_MAP_RESOLUTION = 4096;

struct Vertex {
	glm::vec3 Position;
	float U;
	glm::vec3 Normal;
	float V;
};
struct MeshletBounds {
	glm::vec3 sphereCenter;
	float sphereRadius;

	glm::vec3 coneTip;
	float coneCutoff;

	glm::vec3 coneDirection;
	uint32_t flags;
};
//struct MaterialIndexGroup {
//	uint32_t diffuse;
//	uint32_t metallicRoughness;
//	uint32_t emissive;
//	uint32_t pad;
//};

struct Light {
	glm::vec3 pos;
	float radius;

	glm::vec3 color;
	float falloff;

	glm::vec4 lightDir;

	float cutoff;
	float innerCutoff;
	uint32_t textureID;
	//	LIGHT_POINT = 0,
	//	LIGHT_SPOT  = 1,
	//	LIGHT_DIRECTIONAL = 2,
	//	LIGHT_AREA  = 3
	uint32_t lightType;
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
	// Only used to determine if mesh uses backface culling at the moment.
	uint32_t flags;
};
struct SceneInfo {
	uint32_t meshCount;
	uint32_t pointLightCount;
	uint32_t spotLightCount;
	uint32_t dirLightCount;
	uint32_t windowWidth;
	uint32_t windowHeight;
	uint32_t tileCountX;
	uint32_t tileCountY;
};
struct BufferAddresses {
	vk::DeviceAddress meshletsAddress;
	vk::DeviceAddress meshletVerticesAddress;

	vk::DeviceAddress meshletTrianglesAddress;
	vk::DeviceAddress meshletBoundsAddress;

	vk::DeviceAddress meshViewBufferAddress;
	vk::DeviceAddress vertexBufferAddress;

	vk::DeviceAddress materialBufferAddress;
	vk::DeviceAddress lightBufferAddress;
};
// Push constants have a guaranteed limit of 128 bytes, however most if not all mesh shading capable GPUs have a limit of 256 bytes.
struct PushConstantData {
	vk::DeviceAddress projView;
	vk::DeviceAddress view;

	vk::DeviceAddress invProj;
	vk::DeviceAddress invView;

	vk::DeviceAddress camPos;
	vk::DeviceAddress sceneInfo;

	vk::DeviceAddress vertexBufferAddress;
	vk::DeviceAddress meshletsAddress;

	vk::DeviceAddress meshletVerticesAddress;
	vk::DeviceAddress meshletTrianglesAddress;

	vk::DeviceAddress meshletBoundsAddress;
	vk::DeviceAddress meshViewBufferAddress;

	vk::DeviceAddress materialBufferAddress;
	vk::DeviceAddress lightBufferAddress;
};
struct Chunk {
	uint32_t blocks[32][32];
	uint32_t x, y;
};

vk::AccelerationStructureKHR TLASFromBLAS(vk::AccelerationStructureKHR blasHandles, vk::Device device, uint32_t primitiveCount, const vk::detail::DispatchLoaderDynamic& dldid);

class Renderer
{
public:
	Renderer(SDL_Window* window, std::atomic<bool>* ready);
	void Draw();

	void Move(float forward, float sideward);
	void Teleport(glm::vec3 pos, glm::vec3 dir = glm::vec3(0, 0, 1));
	float yaw = 0;
	float pitch = 0;
private:
	// Temporary abstractions.
	void PushConstant_Draw();
	void ImGui_Draw(double frameTime);
	void LoadModels_Init();
	void SpawnLights_Init();
	void CreateSamplers_Init();
	void CreateDescSets_Init();

	void SubmitAndPresent(uint32_t imageIndex);
	void SubmitImmediate(const std::function<void()>& func);
	void Begin(uint32_t imageIndex, vk::RenderingAttachmentInfo& colorAttachment, vk::RenderingAttachmentInfo& depthAttachment, vk::Rect2D& renderArea);
	bool AcquireImageIndex(uint32_t& index);

	bool doVsync = true;
	bool requestedNewSwapchain = false;
	bool requestNewSwapchain = true;
	bool freezeFrustum = false;
	bool doLightCulling = true;
	bool drawUI = true;
	bool showLightHeatmap = false;
	bool useForwardPlusTestShader = false;

	void BuildGlobalTransform();
	void InitImGui(SDL_Window* window);
	void CreatePipeline();
	void CreateFencesAndSemaphores();
	void InitMainObjects(SDL_Window* window, std::atomic<bool>* ready);

	// Textures.
	void CreateDebugTextures();
	std::vector<AllocatedImage> textures;
	vk::Sampler nearestSampler;
	vk::Sampler linearSampler;

	// Light indices.
	AllocatedBuffer lightIndicesBuffer;
	size_t lightIndicesSize{};

	// Light indices in view.
	AllocatedBuffer lightIndicesViewBuffer;
	size_t lightIndicesViewSize{};

	// Tile frustums.
	AllocatedBuffer tileFrustumBuffer;
	size_t tileFrustumsSize{};

	// Tile AABBs.
	AllocatedBuffer tileAABBBuffer;
	size_t tileAABBsSize{};

	// Tile Depths
	AllocatedBuffer tileDepthsBuffer;
	size_t tileDepthsSize{};

	// Buffer addresses.
	AllocatedBuffer addressBuffer;
	BufferAddresses bufferAddresses{};

	// Descriptor sets.
	std::vector<vk::DescriptorSetLayout> descriptorLayouts;

	vk::AccelerationStructureKHR BLASFromMesh(uint32_t meshVerticesCount, uint32_t primitiveCount, uint32_t firstIndex, uint32_t firstVertex);
	void LoadGLTF(const std::filesystem::path& path, Uploader& uploader, glm::mat4 transform = glm::mat4(1.0f));
	fastgltf::Parser parser;

	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indicesBuffer;
	AllocatedBuffer stageBuffer;
	VmaAllocator allocator{};

	std::vector<AllocatedBuffer> BLASBuffers;

	AllocatedBuffer meshletsBuffer;
	AllocatedBuffer meshletBoundsBuffer;
	AllocatedBuffer meshletVerticesBuffer;
	AllocatedBuffer meshletTrianglesBuffer;

	AllocatedBuffer meshViewBuffer;
	AllocatedBuffer materialBuffer;
	AllocatedBuffer lightBuffer;

	AllocatedBuffer projViewBuffer;
	AllocatedBuffer dirShadowTransBuffer;
	AllocatedBuffer viewBuffer;
	AllocatedBuffer invProjBuffer;
	AllocatedBuffer invViewBuffer;
	AllocatedBuffer camPosBuffer;
	AllocatedBuffer sceneInfoBuffer;

	glm::mat4 dirShadowTrans{};
	glm::mat4 projViewTransform{};
	glm::mat4 view{};
	glm::mat4 proj{};
	glm::vec3 position  = glm::vec3(0);
	glm::vec3 direction = glm::vec3(0, 0, 1.0f);

	ImVec4 clearColorUI;

	Device device;
	Swapchain swapchain;
	AllocatedImage depthImage;
	AllocatedImage shadowMapImage;
	vk::ImageSubresourceRange depthSubresourceRange;
	vk::ImageSubresourceRange depthStencilSubresourceRange;

	Descriptors descriptors;
	Instance instance;
	Timer frameTimer;

	Command graphicsComputeCommand;
	vk::Queue graphicsComputeQueue;

	vk::PipelineLayout pipelineLayout;
	vk::detail::DispatchLoaderDynamic dldid;

	uint32_t currentFrame = 0;
	std::array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> graphCompCmdBuffers;
	std::array <vk::Semaphore, MAX_FRAMES_IN_FLIGHT> imageAcquiredSemaphores;
	std::array <vk::Semaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedSemaphores;
	std::array <vk::Fence, MAX_FRAMES_IN_FLIGHT> inFlightFences;
	std::array <AllocatedBuffer, MAX_FRAMES_IN_FLIGHT> stageBuffers;
	vk::Fence immediateFence;

	void AddMeshlets(std::span<uint32_t> indices, std::span<float> positions, uint32_t prevVerticesSize, uint32_t meshID);
	std::vector<Vertex>			 vertices;
	std::vector<uint32_t>		 indices;
	std::vector<meshopt_Meshlet> meshlets;
	std::vector<MeshletBounds>	 meshletBounds;
	std::vector<uint32_t>		 meshletVertices;
	std::vector<uint8_t>		 meshletTriangles;

	std::vector<MeshView>			meshViews;
	std::vector<MaterialIndexGroup> materialIndexGroups;
	std::vector<uint32_t>			materialIndices;

	std::vector<Light> lights;
	std::vector<Light> pointLights;
	std::vector<Light> spotLights;
	std::vector<Light> dirLights;

	std::vector<vk::ShaderEXT> forwardShaders;
	std::vector<vk::ShaderEXT> forwardPlusShaders;
	std::vector<vk::ShaderEXT> forwardPlusTestShaders;
	std::vector<vk::ShaderEXT> lightHeatmapShaders;
	std::vector<vk::ShaderEXT> depthprepassShaders;
	std::vector<vk::ShaderEXT> shadowMapShaders;
	vk::ShaderEXT minMaxDepthShader;
	vk::ShaderEXT lightCullingShader;
	vk::ShaderEXT lightCullingViewShader;
	vk::ShaderEXT lightCullingTestShader;
	vk::ShaderEXT screenTileFrustumsShader;

	std::array<vk::ShaderStageFlagBits, 4> meshStages = {
	vk::ShaderStageFlagBits::eVertex,
	vk::ShaderStageFlagBits::eTaskEXT,
	vk::ShaderStageFlagBits::eMeshEXT,
	vk::ShaderStageFlagBits::eFragment
	};
};