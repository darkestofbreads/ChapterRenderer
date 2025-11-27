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

struct Frustum {
	Plane planes[6];
};

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

//float4x4 Inverse(float4x4 m) {
//    float n11 = m[0][0], n12 = m[1][0], n13 = m[2][0], n14 = m[3][0];
//    float n21 = m[0][1], n22 = m[1][1], n23 = m[2][1], n24 = m[3][1];
//    float n31 = m[0][2], n32 = m[1][2], n33 = m[2][2], n34 = m[3][2];
//    float n41 = m[0][3], n42 = m[1][3], n43 = m[2][3], n44 = m[3][3];
//
//    float t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
//    float t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
//    float t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
//    float t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
//
//    float det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
//    float idet = 1.0f / det;
//
//    float4x4 ret;
//
//    ret[0][0] = t11 * idet;
//    ret[0][1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet;
//    ret[0][2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet;
//    ret[0][3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet;
//
//    ret[1][0] = t12 * idet;
//    ret[1][1] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet;
//    ret[1][2] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet;
//    ret[1][3] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet;
//
//    ret[2][0] = t13 * idet;
//    ret[2][1] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet;
//    ret[2][2] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet;
//    ret[2][3] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet;
//
//    ret[3][0] = t14 * idet;
//    ret[3][1] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet;
//    ret[3][2] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet;
//    ret[3][3] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet;
//
//    return ret;
//}

float4x4 Inverse(float4x4 m) {
    float
      a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3],
      a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3],
      a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3],
      a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3],

          b00 = a00 * a11 - a01 * a10,
      b01 = a00 * a12 - a02 * a10,
      b02 = a00 * a13 - a03 * a10,
          b03 = a01 * a12 - a02 * a11,
      b04 = a01 * a13 - a03 * a11,
      b05 = a02 * a13 - a03 * a12,
          b06 = a20 * a31 - a21 * a30,
      b07 = a20 * a32 - a22 * a30,
      b08 = a20 * a33 - a23 * a30,
          b09 = a21 * a32 - a22 * a31,
      b10 = a21 * a33 - a23 * a31,
      b11 = a22 * a33 - a23 * a32,

          det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;

    return float4x4(
      a11 * b11 - a12 * b10 + a13 * b09,
      a02 * b10 - a01 * b11 - a03 * b09,
      a31 * b05 - a32 * b04 + a33 * b03,
                a22 * b04 - a21 * b05 - a23 * b03,
      a12 * b08 - a10 * b11 - a13 * b07,
      a00 * b11 - a02 * b08 + a03 * b07,
                a32 * b02 - a30 * b05 - a33 * b01,
      a20 * b05 - a22 * b02 + a23 * b01,
      a10 * b10 - a11 * b08 + a13 * b06,
                a01 * b08 - a00 * b10 - a03 * b06,
      a30 * b04 - a31 * b02 + a33 * b00,
      a21 * b02 - a20 * b04 - a23 * b00,
                a11 * b07 - a10 * b09 - a12 * b06,
      a00 * b09 - a01 * b07 + a02 * b06,
      a31 * b01 - a30 * b03 - a32 * b00,
                a20 * b03 - a21 * b01 + a22 * b00) / det;
}

float4 ClipToView(float4 clip, float4x4 invProj) {
    var view = mul(invProj, clip);
    view = view / view.w;
    return view;
}

#endif