#ifndef _COMMON_H_
#define _COMMON_H_

struct Meshlet {
	uint vertexOffset;
	uint triangleOffset;
	uint vertexCount;
	uint triangleCount;
};

struct MeshView {
	int start;
	int end;
	int material;
	int filler;
};

struct SceneInfo {
	uint meshCount;
	uint pointLightCount;
	uint spotLightCount;
	uint directionLightCount;
};

struct Material {
	uint diffuse;
	uint metallicRoughness;
	uint emmisive;
};

struct Vertex {
	vec3 Position;
	float U;
	vec3 Normal;
	float V;
};

struct PointLight {
	vec3 pos;
	float radius;
	vec3 color;
	float falloff;
};

struct DirLight {
	vec4 lightDir;
	vec4 color;
};

struct SpotLight {
	vec3 pos;
	float radius;
	vec4 lightDir;
	vec3 color;
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