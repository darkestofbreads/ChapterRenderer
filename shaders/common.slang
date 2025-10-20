#ifndef _COMMONSLANG_H_
#define _COMMONSLANG_H_

// local_size_x/y should depend on the tile size as follows:
// 8x8 tile   : 8x8   threads per group
// 16x16 tile : 16x16 threads per group
// 32x32 tile : 32x32 threads per group
#define TILE_SIZE 16

#define MAX_VISIBLE_LIGHTS 128

struct MeshletBounds
{
	float3 sphereCenter;
	float sphereRadius;

	float3 coneTip;
	float coneCutoff;

	float3 coneDirection;
	uint meshID;
};

struct Meshlet {
	uint vertexOffset;
	uint triangleOffset;
	uint vertexCount;
	uint triangleCount;
};

struct MeshView {
	uint start;
	uint end;
	uint material;
	uint flags;
};

struct SceneInfo {
	uint meshCount;
	uint pointLightCount;
	uint spotLightCount;
	uint dirLightCount;
	uint width;
	uint height;
	uint tileCountX;
	uint tileCountY;
};

struct Material {
	uint diffuse;
	uint metallicRoughness;
	uint emmisive;
};

struct Plane
{
	float3 normal;
	float dist;
};

struct Frustum {
	Plane planes[6];
};

struct Vertex {
	float3  position;
	float u;
	float3  normal;
	float v;
};

struct Sphere {
	float3 pos;
	float r;
};

struct Cone {
	float3 tip;
	float height;
	float3 dir;
	float r;
};

//enum LIGHTTYPE {
//	LIGHT_POINT = 0,
//	LIGHT_SPOT  = 1,
//	LIGHT_DIRECTIONAL = 2,
//	LIGHT_AREA  = 3
//};
struct Light {
	float3 pos;
	float radius;

	float3 color;
	float falloff;

	float4 lightDir;

	float cutoff;
	float innerCutoff;
	uint textureID;
	uint lightType;
};

struct PointLight {
	float3  pos;
	float radius;
	float3  color;
	float falloff;
};

struct DirLight {
	float4 lightDir;
	float4 color;
};

struct SpotLight {
	float3  pos;
	float radius;
	float4  lightDir;
	float3  color;
	float falloff;
	float cutoff;
	float innerCutoff;
	float fillerA;
	float fillerB;
};

static float PI = 3.14159265359;
static float PIinv = 1 / PI;

float3 FresnelSchlick(float NdotH, float3 F0) {
	return F0 + pow(clamp(1.0 - NdotH, 0.0, 1.0), 5.0) * (float3(1.0) - F0);
}
float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	//  mix(NdotH2, a2, 1.0) - (a2 - 1)
	//  NdotH2*a2 - NdotH2 + 1.0
	denom = PI * denom * denom;

	return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) * 0.125;
	
	float denom = lerp(NdotV, 1.0, k);
	return NdotV / denom;
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

#endif