#version 450

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_GOOGLE_include_directive : enable

#include "../common.h"

layout(set = 0, binding = 2) readonly buffer LightIndices {
    int lightIndices[];
};

layout(push_constant, std430) uniform constant
{
	mat4 projView;
	mat4 view;
	mat4 proj;
	vec4 camPos;
	SceneInfo sceneInfo;
};

layout(location = 0) out vec4 outColor;
void main() {
	vec3 fragment = vec3(0);
	
	ivec2 tileID   = ivec2(gl_FragCoord.xy / TILE_SIZE);
	uint tileIndex = tileID.y * sceneInfo.tileCountX + tileID.x;
	uint offset = tileIndex * MAX_VISIBLE_LIGHTS;

	uint i;
	uint visiblePointLights = 0;
	for (i = 0; i < MAX_VISIBLE_LIGHTS; i++) {
		int lightindex = lightIndices[i + offset];
		if (lightindex == -1) break;
	}

	uint visibleSpotLights = 0;
	for (i++; i < MAX_VISIBLE_LIGHTS; i++) {
		int lightindex = lightIndices[i + offset];
		if (lightindex == -1) break;
	}
	i -= 3;

	i += sceneInfo.dirLightCount;
	float b = 1.0 / (MAX_VISIBLE_LIGHTS * (1.0/max(i, 0.0001)));
	float g = b * (i / 10.0);
	outColor = vec4(0, g, b, 1);
}