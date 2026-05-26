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
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> minMaxDepthPerTile;
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

    // Compute min and max depth of tile
    {
        let uv = cs.GlobalInvocationID.xy / float2(pc.sceneInfo.width, pc.sceneInfo.height);
        let depth    = depth.Sample(uv).x;
        let depthInt = asuint(depth);

        InterlockedMax(minDepthInt, depthInt);
        InterlockedMin(maxDepthInt, depthInt);
    }

    GroupMemoryBarrierWithGroupSync();

    if(cs.LocalInvocationIndex == 0) {
        let tileID = cs.WorkGroupID.xy;
        let index = (tileID.y * pc.sceneInfo.tileCountX + tileID.x) * 2;
        minMaxDepthPerTile[index] = minDepthInt;
        minMaxDepthPerTile[index + 1] = maxDepthInt;

        // Very slow, use only for debugging purposes
        // Compute min and max depth of view
        //InterlockedMax(minMaxDepthPerTile[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY * 2], minDepthInt);
        //InterlockedMin(minMaxDepthPerTile[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY * 2 + 1], maxDepthInt);
    }

    GroupMemoryBarrierWithGroupSync();

    if(cs.GlobalInvocationID.x < pc.sceneInfo.tileCountX && cs.GlobalInvocationID.y < pc.sceneInfo.tileCountY) {
        let index = (cs.GlobalInvocationID.y * pc.sceneInfo.tileCountX + cs.GlobalInvocationID.x) * 2;
        let minDepth = minMaxDepthPerTile[index];
        let maxDepth = minMaxDepthPerTile[index + 1];
        InterlockedMin(maxDepthInt, maxDepth);
        InterlockedMax(minDepthInt, minDepth);
    } else {
        return;
    }

    GroupMemoryBarrierWithGroupSync();

    if(cs.LocalInvocationIndex == 0) {
        InterlockedMin(minMaxDepthPerTile[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY * 2 + 1], maxDepthInt);
        InterlockedMax(minMaxDepthPerTile[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY * 2], minDepthInt);
    } else {
        return;
    }

    GroupMemoryBarrierWithGroupSync();

    // Compute view frustum
    if (cs.GlobalInvocationID.x == 0 && cs.GlobalInvocationID.y == 0) {
        float minDepth = asfloat(minMaxDepthPerTile[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY * 2]);
    	float maxDepth = asfloat(minMaxDepthPerTile[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY * 2 + 1]);

        float minDepthView = ClipToView(float4(0, 0, minDepth, 1), *pc.invProj).z;
        float maxDepthView = ClipToView(float4(0, 0, maxDepth, 1), *pc.invProj).z;

        Plane nearPlane;
        nearPlane.normal = float3(0, 0, -1);
        nearPlane.dist   = -minDepthView;
        Plane farPlane;
        farPlane.normal = float3(0, 0, 1);
        farPlane.dist   = maxDepthView;

        float4 screenSpace[4];
        screenSpace[0] = float4(float2(0, 0), -1.0, 1.0);
        screenSpace[1] = float4(float2(pc.sceneInfo.width, 0), -1.0, 1.0);
        screenSpace[2] = float4(float2(0, pc.sceneInfo.height), -1.0, 1.0);
        screenSpace[3] = float4(float2(pc.sceneInfo.width, pc.sceneInfo.height), -1.0, 1.0);

        float3 viewSpace[4];
        for (int i = 0; i < 4; i++)
            viewSpace[i] = ScreenToView(screenSpace[i], *pc.sceneInfo, *pc.invProj).xyz;

        Frustum f;
        let eyePos = float3(0, 0, 0);
        f.planes[0] = ComputePlane(eyePos, viewSpace[2], viewSpace[0]);
        f.planes[1] = ComputePlane(eyePos, viewSpace[1], viewSpace[3]);
        f.planes[2] = ComputePlane(eyePos, viewSpace[0], viewSpace[1]);
        f.planes[3] = ComputePlane(eyePos, viewSpace[3], viewSpace[2]);
        f.planes[4] = nearPlane;
        f.planes[5] = farPlane;

        tileFrustums[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY] = f;
        //tileFrustums[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY + 1] = ExtractFrustum(*pc.projView);
    }
}