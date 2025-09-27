#ifndef _COMMON_H_
#define _COMMON_H_

// local_size_x/y should depend on the tile size as follows:
// 8x8 tile   : 8x8   threads per group
// 16x16 tile : 16x16 threads per group
// 32x32 tile : 32x32 threads per group
#define TILE_SIZE 16

#define MAX_VISIBLE_LIGHTS 128

struct MeshletBounds
{
	vec3 sphereCenter;
	float sphereRadius;

	vec3 coneTip;
	float coneCutoff;

	vec3 coneDirection;
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
	vec3 normal;
	float dist;
};

struct Frustum {
	Plane planes[6];
};

struct Vertex {
	vec3  position;
	float u;
	vec3  normal;
	float v;
};

struct Sphere {
	vec3 pos;
	float r;
};

struct Cone {
	vec3 tip;
	float height;
	vec3 dir;
	float r;
};

//enum LIGHTTYPE {
//	LIGHT_POINT = 0,
//	LIGHT_SPOT  = 1,
//	LIGHT_DIRECTIONAL = 2,
//	LIGHT_AREA  = 3
//};
struct Light {
	vec3 pos;
	float radius;

	vec3 color;
	float falloff;

	vec4 lightDir;

	float cutoff;
	float innerCutoff;
	uint textureID;
	uint lightType;
};

struct PointLight {
	vec3  pos;
	float radius;
	vec3  color;
	float falloff;
};

struct DirLight {
	vec4 lightDir;
	vec4 color;
};

struct SpotLight {
	vec3  pos;
	float radius;
	vec4  lightDir;
	vec3  color;
	float falloff;
	float cutoff;
	float innerCutoff;
	float fillerA;
	float fillerB;
};

const float PI = 3.14159265359;
const float PIinv = 1 / PI;

vec3 FresnelSchlick(float NdotH, vec3 F0) {
	return F0 + pow(clamp(1.0 - NdotH, 0.0, 1.0), 5.0) * (vec3(1.0) - F0);
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

	float denom = mix(NdotV, 1.0, k);
	return NdotV / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

#endif