#include "common.slang"

[[vk::binding(4, 0)]]RWStructuredBuffer<Frustum> tileFrustums;

struct BufferAdresses {
    Meshlet* meshlets;
    uint* meshletVertices;
    uint8_t* meshletTriangles;
    MeshletBounds* meshletBounds;

    Vertex* vertices;
    Material* materials;
    Light* lights;
    MeshView* meshViews;
};

[[vk::binding(3, 0)]] RWStructuredBuffer<BufferAdresses> bufferAddresses;

struct PushConstant
{
	float4x4 projView;
	float4x4 view;
	float4x4 proj;
	float4 camPos;
	SceneInfo sceneInfo;
};
[[vk_push_constant]] PushConstant pc;

float4x4 Inverse(float4x4 m) {
    float n11 = m[0][0], n12 = m[1][0], n13 = m[2][0], n14 = m[3][0];
    float n21 = m[0][1], n22 = m[1][1], n23 = m[2][1], n24 = m[3][1];
    float n31 = m[0][2], n32 = m[1][2], n33 = m[2][2], n34 = m[3][2];
    float n41 = m[0][3], n42 = m[1][3], n43 = m[2][3], n44 = m[3][3];

    var t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
    var t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
    var t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
    var t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;

    var det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
    var idet = 1.0f / det;

    float4x4 ret;

    ret[0][0] = t11 * idet;
    ret[0][1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet;
    ret[0][2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet;
    ret[0][3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet;

    ret[1][0] = t12 * idet;
    ret[1][1] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet;
    ret[1][2] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet;
    ret[1][3] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet;

    ret[2][0] = t13 * idet;
    ret[2][1] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet;
    ret[2][2] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet;
    ret[2][3] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet;

    ret[3][0] = t14 * idet;
    ret[3][1] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet;
    ret[3][2] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet;
    ret[3][3] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet;

    return ret;
}
float4 ClipToView(float4 clip, float4x4 proj) {
    var view = mul(clip, Inverse(proj));
    view = view / view.w;
    return view;
}
float4 ScreenToView(float4 screen, SceneInfo sceneInfo, float4x4 proj) {
    var coord = screen.xy / float2(sceneInfo.width, sceneInfo.height);
    var clip  = float4(float2(coord.x, 1.0 - coord.y) * 2.0 - 1.0, screen.z, screen.w);
    return ClipToView(clip, proj);
}

Plane ComputePlane(float3 p0, float3 p1, float3 p2) {
    Plane plane;
    var v0 = p1 - p0;
    var v1 = p2 - p0;

    plane.normal = normalize( cross(v0, v1) );
    plane.dist   = dot(plane.normal, p0);
    return plane;
}

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void computeMain(uint3 id: SV_DispatchThreadID) {
    if (id.x >= pc.sceneInfo.tileCountX || id.y >= pc.sceneInfo.tileCountY) return;
    let index = id.y * pc.sceneInfo.tileCountX + id.x;
    
    float4 screenSpace[4];
    screenSpace[0] = float4(float2(id.x,     id.y) * TILE_SIZE, -1.0, 1.0);
    screenSpace[1] = float4(float2(id.x + 1, id.y) * TILE_SIZE, -1.0, 1.0);
    screenSpace[2] = float4(float2(id.x,     id.y + 1) * TILE_SIZE, -1.0, 1.0);
    screenSpace[3] = float4(float2(id.x + 1, id.y + 1) * TILE_SIZE, -1.0, 1.0);

    float3 viewSpace[4];
    for (int i = 0; i < 4; i++)
        viewSpace[i] = ScreenToView(screenSpace[i], pc.sceneInfo, pc.proj).xyz;

    Frustum f;
    let eyePos = float3( 0, 0, 0 );
    f.planes[0] = ComputePlane(eyePos, viewSpace[2], viewSpace[0]);
    f.planes[1] = ComputePlane(eyePos, viewSpace[1], viewSpace[3]);
    f.planes[2] = ComputePlane(eyePos, viewSpace[0], viewSpace[1]);
    f.planes[3] = ComputePlane(eyePos, viewSpace[3], viewSpace[2]);
    tileFrustums[index] = f;
}