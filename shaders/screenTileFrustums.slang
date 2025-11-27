#include "common.slang"

[[vk::binding(4, 0)]] RWStructuredBuffer<Frustum> tileFrustums;

float4 ScreenToView(float4 screen, SceneInfo sceneInfo, float4x4 invProj) {
    var coord = screen.xy / float2(sceneInfo.width, sceneInfo.height);
    var clip = float4(float2(coord.x, 1.0 - coord.y) * 2.0 - 1.0, screen.z, screen.w);
    return ClipToView(clip, invProj);
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
void computeMain(uint3 id: SV_DispatchThreadID, uniform PushConstant pc) {
    if (id.x >= pc.sceneInfo.tileCountX || id.y >= pc.sceneInfo.tileCountY) return;
    let index = id.y * pc.sceneInfo.tileCountX + id.x;
    
    float4 screenSpace[4];
    screenSpace[0] = float4(float2(id.x,     id.y) * TILE_SIZE, -1.0, 1.0);
    screenSpace[1] = float4(float2(id.x + 1, id.y) * TILE_SIZE, -1.0, 1.0);
    screenSpace[2] = float4(float2(id.x,     id.y + 1) * TILE_SIZE, -1.0, 1.0);
    screenSpace[3] = float4(float2(id.x + 1, id.y + 1) * TILE_SIZE, -1.0, 1.0);

    float3 viewSpace[4];
    for (int i = 0; i < 4; i++)
        viewSpace[i] = ScreenToView(screenSpace[i], *pc.sceneInfo, *pc.invProj).xyz;

    Frustum f;
    let eyePos = float3( 0, 0, 0 );
    f.planes[0] = ComputePlane(eyePos, viewSpace[2], viewSpace[0]);
    f.planes[1] = ComputePlane(eyePos, viewSpace[1], viewSpace[3]);
    f.planes[2] = ComputePlane(eyePos, viewSpace[0], viewSpace[1]);
    f.planes[3] = ComputePlane(eyePos, viewSpace[3], viewSpace[2]);
    tileFrustums[index] = f;
}