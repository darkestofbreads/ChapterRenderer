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

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(buffer_reference, std430) readonly buffer MeshletBuffer{ 
	Meshlet meshlets[];
};
layout(buffer_reference, std430) readonly buffer MeshletVertexBuffer{ 
	uint meshletVertices[];
};
layout(buffer_reference, std430) readonly buffer MeshletTriangleBuffer{ 
	uint8_t meshletTriangles[];
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
	Vertex vertices[];
};
layout(buffer_reference, std430) readonly buffer MaterialBuffer{
	Material materials[];
};
layout(buffer_reference, std430) readonly buffer PointLightBuffer{
	PointLight pointLights[];
};
layout(buffer_reference, std430) readonly buffer DirLightBuffer{
	DirLight dirLights[];
};
layout(buffer_reference, std430) readonly buffer SpotLightBuffer{
	SpotLight spotLights[];
};
layout(buffer_reference, std430) readonly buffer MeshViewBuffer{
	MeshView meshViews[];
};
layout(push_constant, std430) uniform constant
{
	mat4 projView;
	mat4 worldTransform;
	SceneInfo sceneInfo;

	MeshletBuffer meshletBuffer;
	MeshletVertexBuffer meshletVertices;
	MeshletTriangleBuffer meshletTriangles;

	MeshViewBuffer meshViewBuffer;
	VertexBuffer vertexBuffer;
	MaterialBuffer materialBuffer;

	PointLightBuffer pointLightBuffer;
	SpotLightBuffer spotLightBuffer;
	DirLightBuffer dirLightBuffer;
};

vec3 CalcPointLight(PointLight light, vec3 V, vec3 N, vec3 albedo, vec4 metallicRoughness) {
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
vec3 CalcDirLight(DirLight light, vec3 V, vec3 N, vec3 albedo, vec4 metallicRoughness) {
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
vec3 CalcSpotLight(SpotLight light, vec3 V, vec3 N, vec3 albedo, vec4 metallicRoughness) {
	vec3 L      = normalize(light.pos - V);
	float theta = dot(L, -normalize(light.lightDir.xyz));
	if(theta < light.cutoff)
		return vec3(0.0, 0.0, 0.0);

	vec3 H			= normalize(L - V);
	float epsilon   = light.innerCutoff - light.cutoff;
	float intensity = clamp((theta - light.cutoff) / epsilon, 0.0, 1.0); 

//	float dist        = distance(light.pos.xyz, V);
//	float attenuation = 1 / (dist * dist);
//	vec3 radiance     =  attenuation * light.color.xyz;
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

layout(location = 0) out vec4 outColor;
void main() {
// Implement range discard for each point and spot light.
// TODO: Forward+
// TODO: further optimize shader to use MAD instructions and built in operators
	Material mat = materialBuffer.materials[materialIndex];
	
	vec3 fragment = vec3(0);
	vec3 N = normalize(normal);
	vec4 difFrag = texture(textures[mat.diffuse], uv);
	vec4 metallicRoughness = texture(textures[mat.metallicRoughness], uv);

	mat4 normalTransform = transpose(inverse(worldTransform));

	// Lighting calculations.
	// Possibly move updates of light positions and normals to a compute shader.
	for(int i = 0; i < sceneInfo.pointLightCount; i++) {
			PointLight pointLight = pointLightBuffer.pointLights[i];
			pointLight.pos = (worldTransform * vec4(pointLight.pos, 1)).xyz;
			fragment += CalcPointLight(pointLight, pos, N, difFrag.xyz, metallicRoughness);
	}
	for(int i = 0; i < sceneInfo.spotLightCount; i++) {
			SpotLight spotLight = spotLightBuffer.spotLights[i];
			spotLight.pos = (worldTransform * vec4(spotLight.pos, 1)).xyz;
			spotLight.lightDir = normalTransform * spotLight.lightDir;
			fragment += CalcSpotLight(spotLight, pos, N, difFrag.xyz, metallicRoughness);
	}
	for(int i = 0; i < sceneInfo.directionLightCount; i++) {
			DirLight dirLight = dirLightBuffer.dirLights[i];
			dirLight.lightDir = normalTransform * dirLight.lightDir;
			fragment += CalcDirLight(dirLight, pos, N, difFrag.xyz, metallicRoughness);
	}

	// Gamma correction.
	fragment = fragment / (fragment + vec3(1.0));
	fragment = pow(fragment, vec3(1.0/2.2));
	
	// Add emissive to final pixel.
	vec4 emissiveFrag = texture(textures[mat.emmisive], uv);
	outColor = mix(vec4(fragment, 1), emissiveFrag, dot(emissiveFrag.xyz, vec3(1)));
}