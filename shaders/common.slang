#ifndef _COMMONSLANG_H_
#define _COMMONSLANG_H_

// local_size_x/y should depend on the tile size as follows:
// 8x8 tile   : 8x8   threads per group
// 16x16 tile : 16x16 threads per group
// 32x32 tile : 32x32 threads per group
#define TILE_SIZE 16

#define MAX_VISIBLE_LIGHTS 128

struct PushConstant {
    float4x4 *projView;
    float4x4 *view;

    float4x4 *invProj;
    float4x4 *invView;

    float4 *camPos;
    SceneInfo *sceneInfo;

    Vertex *vertices;
    Meshlet *meshlets;

    uint *meshletVertices;
    uint8_t *meshletTriangles;

    MeshletBounds *meshletBounds;
    MeshView *meshViews;

    Material *materials;
    Light *lights;
};

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
    uint filler;
};

struct Plane
{
	float3 normal;
	float dist;
};

Plane ComputePlane(float3 p0, float3 p1, float3 p2) {
    Plane plane;
    var v0 = p1 - p0;
    var v1 = p2 - p0;

    plane.normal = normalize(cross(v0, v1));
    plane.dist = dot(plane.normal, p0);
    return plane;
}

struct Frustum {
	Plane planes[6];
};

struct AABB {
    float4 min;
    float4 max;
    float4 size;
    float4 center;
}
AABB ComputeAABB(float4 min, float4 max) {
    AABB aabb;
    aabb.min = min;
    aabb.max = max;
    aabb.center = (min + max) * 0.5;
    aabb.size = abs(max - aabb.center);

    return aabb;
}
AABB ComputeAABB(float3 min, float3 max) {
    return ComputeAABB(float4(min, 1.0), float4(max, 1.0));
}

struct Vertex {
	float3 position;
	float u;
	float3 normal;
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

float4 ClipToView(float4 clip, float4x4 invProj) {
    var view = mul(clip, invProj);
    view = view / view.w;
    return view;
}
float4 ScreenToView(float4 screen, SceneInfo sceneInfo, float4x4 invProj) {
    var coord = screen.xy / float2(sceneInfo.width, sceneInfo.height);
    var clip = float4(float2(coord.x, 1.0 - coord.y) * 2.0 - 1.0, screen.z, screen.w);
    return ClipToView(clip, invProj);
}

#endif