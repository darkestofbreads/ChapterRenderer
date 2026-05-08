#include "common.slang"

struct ComputeShaderInput
{
    uint3 WorkGroupID : SV_GroupID;
    uint3 LocalInvocationID : SV_GroupThreadID;
    uint3 GlobalInvocationID : SV_DispatchThreadID;
    uint LocalInvocationIndex : SV_GroupIndex;
};

[[vk::binding(1, 0)]] Sampler2D depth;
[[vk::binding(4, 0)]] RWStructuredBuffer<Frustum> tileFrustums;
groupshared uint minDepthInt;
groupshared uint maxDepthInt;

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void computeMain(ComputeShaderInput cs, uniform PushConstant pc) {
    if (cs.LocalInvocationIndex == 0) {
	    minDepthInt = 0;
	    maxDepthInt = 0xffffffff;
	}

    GroupMemoryBarrierWithGroupSync();

    // Compute min and max depth of tile.
    let uv = cs.GlobalInvocationID.xy / float2(pc.sceneInfo.width, pc.sceneInfo.height);
    let depth    = depth.Sample(uv).x;
    let depthInt = asuint(depth);

    InterlockedMin(maxDepthInt, depthInt);
    InterlockedMax(minDepthInt, depthInt);

    GroupMemoryBarrierWithGroupSync();

    // Compute tile frustum.
    float minDepth = asfloat(minDepthInt);
	float maxDepth = asfloat(maxDepthInt);
    float minDepthView = ClipToView(float4(0, 0, minDepth, 1), *pc.invProj).z;
    float maxDepthView = ClipToView(float4(0, 0, maxDepth, 1), *pc.invProj).z;
    float nearClipView = ClipToView(float4(0, 0, 0, 1)       , *pc.invProj).z;

    Plane minPlane;
    minPlane.normal = float3(0, 0, -1);
    minPlane.dist   = -minDepthView;

    GroupMemoryBarrierWithGroupSync();

    if (cs.GlobalInvocationID.x == 0 && cs.GlobalInvocationID.y == 0 && cs.LocalInvocationIndex == 0) {
            float4 screenSpace[4];
            screenSpace[0] = float4(float2(0, 0), -1.0, 1.0);
            screenSpace[1] = float4(float2(1 * pc.sceneInfo.width, 0), -1.0, 1.0);
            screenSpace[2] = float4(float2(0, 1 * pc.sceneInfo.height), -1.0, 1.0);
            screenSpace[3] = float4(float2(1 * pc.sceneInfo.width, 1 * pc.sceneInfo.height), -1.0, 1.0);

            float3 viewSpace[4];
            for (int i = 0; i < 4; i++)
                viewSpace[i] = ScreenToView(screenSpace[i], *pc.sceneInfo, *pc.invProj).xyz;

            Frustum f;
            let eyePos = float3(0, 0, 0);
            f.planes[0] = ComputePlane(eyePos, viewSpace[2], viewSpace[0]);
            f.planes[1] = ComputePlane(eyePos, viewSpace[1], viewSpace[3]);
            f.planes[2] = ComputePlane(eyePos, viewSpace[0], viewSpace[1]);
            f.planes[3] = ComputePlane(eyePos, viewSpace[3], viewSpace[2]);
            tileFrustums[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY] = f;
        }
}