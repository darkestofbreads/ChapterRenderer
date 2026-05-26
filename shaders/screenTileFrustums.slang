#include "common.slang"

[[vk::binding(4, 0)]] RWStructuredBuffer<Frustum> tileFrustums;

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void computeMain(uint3 id: SV_DispatchThreadID, uint localInvocationIndex: SV_GroupIndex, uniform PushConstant pc) {
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