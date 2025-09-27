#version 450

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_GOOGLE_include_directive : enable

#include "common.h"

layout(location = 1) in vec2 uv;
layout(location = 2) flat in uint materialIndex;
layout(location = 3) in vec3 pos;
layout(location = 4) in vec3 normal;

layout(set = 0, binding = 0) uniform sampler2D Textures[];
layout(set = 0, binding = 1) uniform sampler2D Depth;
layout(set = 0, binding = 2) readonly buffer LightIndices {
    int lightIndices[];
};

layout(buffer_reference, std430) readonly buffer MeshletBuffer{ 
	Meshlet meshlets[];
};
layout(buffer_reference, std430) readonly buffer MeshletVertexBuffer{ 
	uint meshletVertices[];
};
layout(buffer_reference, std430) readonly buffer MeshletTriangleBuffer{ 
	uint8_t meshletTriangles[];
};
layout(buffer_reference, std430) readonly buffer MeshletBoundBuffer{ 
	MeshletBounds meshletBounds[];
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
	Vertex vertices[];
};
layout(buffer_reference, std430) readonly buffer MaterialBuffer{
	Material materials[];
};
layout(buffer_reference, std430) readonly buffer LightBuffer{
	Light lights[];
};
layout(buffer_reference, std430) readonly buffer MeshViewBuffer{
	MeshView meshViews[];
};
layout(set = 0, binding = 3) uniform BufferAddresses {
	MeshletBuffer meshletBuffer;
	MeshletVertexBuffer meshletVertices;
	MeshletTriangleBuffer meshletTriangles;
	MeshletBoundBuffer meshletBounds;

	MeshViewBuffer meshViewBuffer;
	VertexBuffer vertexBuffer;
	MaterialBuffer materialBuffer;
	LightBuffer lightBuffer;
};

layout(push_constant, std430) uniform constant
{
	mat4 projView;
	mat4 view;
	mat4 proj;
	vec4 camPos;
	SceneInfo sceneInfo;
};

vec3 CalcPointLight(Light light, vec3 V, vec3 N, vec3 albedo, vec4 metallicRoughness) {
	vec3 L = normalize(light.pos - V);
	vec3 H = normalize(L - V);

	float normaDist = distance(light.pos, V) / light.radius;
	if(normaDist >= 1.0)
		return vec3(0);

	float normaDist2  = normaDist * normaDist;
	float attenuation = pow((1 - normaDist2), 2) / (1 + light.falloff * normaDist2);
	vec3 radiance     = attenuation * light.color;

	float NdotH     = max(dot(N, H), 0.0);
	float coverage  = max(dot(L, N), 0.0);

	vec3 F0         = mix(vec3(0.04), albedo, metallicRoughness.z);
	vec3 kspecular  = FresnelSchlick(max(dot(H, -V), 1.0), F0);
	
	float NDF       = DistributionGGX(NdotH, metallicRoughness.y);
	float G         = GeometrySmith(N, -V, L, metallicRoughness.y);

	vec3 num      = NDF * G * kspecular;
	float dom     = 4.0 * max(dot(N, -V), 0.0) * coverage + 0.0001;
	vec3 specular = num / dom;
	
	vec3 kdiffuse = vec3(1.0) - kspecular;

	return (kdiffuse * albedo / PI + specular) * radiance * coverage;
}
vec3 CalcDirLight(Light light, vec3 V, vec3 N, vec3 albedo, vec4 metallicRoughness) {
	vec3 L = -normalize(light.lightDir.xyz);
	vec3 H = normalize(L - V);

	vec3 radiance   = light.color.xyz;

	float NdotH     = max(dot(N, H), 0.0);
	float coverage  = max(dot(L, N), 0.0);
	vec3 F0         = mix(vec3(0.04), albedo, metallicRoughness.z);
	
	float NDF       = DistributionGGX(NdotH, metallicRoughness.y);
	float G         = GeometrySmith(N, -V, L, metallicRoughness.y);
	vec3 kspecular  = FresnelSchlick(max(dot(H, -V), 1.0), F0);

	vec3 num      = NDF * G * kspecular;
	float dom     = 4.0 * max(dot(N, -V), 0.0) * coverage + 0.0001;
	vec3 specular = num / dom;
	
	vec3 kdiffuse = vec3(1.0) - kspecular;

	return (kdiffuse * albedo / PI + specular) * radiance * coverage;
}
vec3 CalcSpotLight(Light light, vec3 V, vec3 N, vec3 albedo, vec4 metallicRoughness) {
	vec3 L      = normalize(light.pos - V);
	float theta = dot(L, -normalize(light.lightDir.xyz));
	if(theta < light.cutoff)
		return vec3(0.0, 0.0, 0.0);

	vec3 H			= normalize(L - V);
	float epsilon   = light.innerCutoff - light.cutoff;
	float intensity = clamp((theta - light.cutoff) / epsilon, 0.0, 1.0); 

	float normaDist = distance(light.pos, V) / light.radius;
	if(normaDist >= 1.0)
		return vec3(0);

	float normaDist2  = normaDist * normaDist;
	float attenuation = pow((1 - normaDist2), 2) / (1 + light.falloff * normaDist2);
	vec3 radiance     = attenuation * light.color;

	float NdotH     = max(dot(N, H), 0.0);
	float coverage  = max(dot(L, N), 0.0);
	vec3 F0         = mix(vec3(0.04), albedo, metallicRoughness.z);
	
	float NDF       = DistributionGGX(NdotH, metallicRoughness.y);
	float G         = GeometrySmith(N, -V, L, metallicRoughness.y);
	vec3 kspecular  = FresnelSchlick(max(dot(H, -V), 1.0), F0);

	vec3 num      = NDF * G * kspecular;
	float dom     = 4.0 * max(dot(N, -V), 0.0) * coverage + 0.0001;
	vec3 specular = num / dom;
	
	vec3 kdiffuse = vec3(1.0) - kspecular;

	return (kdiffuse * albedo / PI + specular) * radiance * coverage * intensity;
}

layout(location = 5) in vec4 meshletColor;

layout(location = 0) out vec4 outColor;
void main() {
	Material mat = materialBuffer.materials[materialIndex];

	vec3 fragment		   = vec3(0);
	vec3 N				   = normalize(normal);
	vec4 difFrag           = texture(Textures[mat.diffuse], uv);
	vec4 metallicRoughness = texture(Textures[mat.metallicRoughness], uv);
	
	mat4 normalTransform = transpose(inverse(view));
	
	ivec2 tileID   = ivec2(gl_FragCoord.xy / TILE_SIZE);
	uint tileIndex = tileID.y * sceneInfo.tileCountX + tileID.x;

	uint offset = tileIndex * MAX_VISIBLE_LIGHTS;
	uint i;
	for (i = 0; i < MAX_VISIBLE_LIGHTS; i++) {
		int lightindex = lightIndices[i + offset];

		if (lightindex == -1) break;

		Light light = lightBuffer.lights[lightindex];
		light.pos = (view * vec4(light.pos, 1)).xyz;
		fragment += CalcPointLight(light, pos, N, difFrag.xyz, metallicRoughness);
	}

	for (i++; i < MAX_VISIBLE_LIGHTS; i++) {
		int lightindex = lightIndices[i + offset];
	
		if (lightindex == -1) break;
	
		Light light = lightBuffer.lights[lightindex];
		light.pos = (view * vec4(light.pos, 1)).xyz;
		light.lightDir = normalTransform * light.lightDir;
	
		fragment += CalcSpotLight(light, pos, N, difFrag.xyz, metallicRoughness);
	}

	uint index = 0;
	uint limit = sceneInfo.pointLightCount;
	index = sceneInfo.spotLightCount;
	limit += sceneInfo.spotLightCount;

	index += sceneInfo.pointLightCount;
	for (limit += sceneInfo.dirLightCount; index < limit; index++) {
			Light light = lightBuffer.lights[index];
			light.lightDir = normalTransform * light.lightDir;
			fragment += CalcDirLight(light, pos, N, difFrag.xyz, metallicRoughness);
	}

	// Gamma correction.
	fragment = fragment / (fragment + vec3(1.0));
	fragment = pow(fragment, vec3(1.0/2.2));
	
	// Add emissive to final pixel.
	vec4 emissiveFrag = texture(Textures[mat.emmisive], uv);
	outColor          = mix(vec4(fragment, 1), emissiveFrag, dot(emissiveFrag.xyz, vec3(1.0)));
}