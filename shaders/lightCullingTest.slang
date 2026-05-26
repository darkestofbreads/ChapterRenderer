#include "common.slang"

[[vk::binding(0, 0)]] RWStructuredBuffer<Sampler2D> textures;
[[vk::binding(1, 0)]] Sampler2D depth;
[[vk::binding(2, 0)]] RWStructuredBuffer<int> lightIndicesInTiles;
[[vk::binding(4, 0)]] RWStructuredBuffer<Frustum> tileFrustums;
[[vk::binding(7, 0)]] RWStructuredBuffer<int> lightIndicesInView;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> minMaxDepthPerTile;

bool SphereInsidePlane(Sphere sphere, Plane plane) {
    return dot(plane.normal, sphere.pos) - plane.dist < -sphere.r;
}
bool SphereInsideFrustum( Sphere sphere, Frustum frustum, float zNear, float zFar ) {
    if (sphere.pos.z - sphere.r > zNear || sphere.pos.z + sphere.r < zFar)
        return false;

    for(uint i = 0; i < 4; i++) {
        if( SphereInsidePlane(sphere, frustum.planes[i]) )
          return false;
    }

    return true;
}

bool PointInsidePlane(float3 p, Plane plane) {
    return dot(plane.normal, p) - plane.dist < 0;
}
bool ConeInsidePlane(Cone cone, Plane plane) {
    let m = cross( cross(plane.normal, cone.dir), cone.dir);
    let q = cone.tip + cone.dir * cone.height - m * cone.r;
    return PointInsidePlane(cone.tip, plane) && PointInsidePlane(q, plane);
}
bool ConeInsideFrustum(Cone cone, Frustum frustum, float zNear, float zFar) {
    Plane near;
    near.normal = float3(0, 0, -1);
    near.dist   = -zNear;
    Plane far;
    far.normal = float3(0, 0, 1);
    far.dist   = zFar;

    if(ConeInsidePlane(cone, near) || ConeInsidePlane(cone, far))
        return false;

    for(uint i = 0; i < 4; i++) {
        if(ConeInsidePlane(cone, frustum.planes[i]))
            return false;
    }

    return true;
}
bool SphereInsideAABB(Sphere s, AABB aabb) {
    let vDelta = max(0, abs(aabb.center.xyz - s.pos) - aabb.size.xyz);
    let fDistSq = dot(vDelta, vDelta);
    return fDistSq <= s.r * s.r;
}

groupshared float minDepthView;
groupshared float maxDepthView;
groupshared uint visiblePointLightsCount;
groupshared uint visibleSpotLightsCount;
groupshared uint offset;
groupshared Frustum frustum;
groupshared AABB aabb;

struct ComputeShaderInput
{
    uint3 WorkGroupID : SV_GroupID;
    uint3 LocalInvocationID : SV_GroupThreadID;
    uint3 GlobalInvocationID : SV_DispatchThreadID;
    uint LocalInvocationIndex : SV_GroupIndex;
};

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void computeMain(ComputeShaderInput cs, uniform PushConstant pc) {
    if (cs.LocalInvocationIndex == 0) {
        let index = cs.WorkGroupID.y * pc.sceneInfo.tileCountX + cs.WorkGroupID.x;
        offset = index * MAX_VISIBLE_LIGHTS;
		visiblePointLightsCount = 0;
        visibleSpotLightsCount  = 0;
        frustum = tileFrustums[index];

        // Compute view depth
        let minDepth = asfloat(minMaxDepthPerTile[index * 2]);
        let maxDepth = asfloat(minMaxDepthPerTile[index * 2 + 1]);
        let nearClipView = ClipToView(float4(0, 0, 0, 1), *pc.invProj).z;
        minDepthView = ClipToView(float4(0, 0, minDepth, 1), *pc.invProj).z;
        maxDepthView = ClipToView(float4(0, 0, maxDepth, 1), *pc.invProj).z;

        // Compute tile AABB
        let tileID = cs.WorkGroupID.xy;
        float3 viewSpace[8];
        viewSpace[0] = ScreenToView(float4(tileID.xy * TILE_SIZE, minDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[1] = ScreenToView(float4(float2(tileID.x + 1, tileID.y) * TILE_SIZE, minDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[2] = ScreenToView(float4(float2(tileID.x, tileID.y + 1) * TILE_SIZE, minDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[3] = ScreenToView(float4(float2(tileID.x + 1, tileID.y + 1) * TILE_SIZE, minDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;

        viewSpace[4] = ScreenToView(float4(tileID.xy * TILE_SIZE, maxDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[5] = ScreenToView(float4(float2(tileID.x + 1, tileID.y) * TILE_SIZE, maxDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[6] = ScreenToView(float4(float2(tileID.x, tileID.y + 1) * TILE_SIZE, maxDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[7] = ScreenToView(float4(float2(tileID.x + 1, tileID.y + 1) * TILE_SIZE, maxDepth, 1.0), *pc.sceneInfo, *pc.invProj).xyz;

        float3 minAABB = 10000000;
        float3 maxAABB = -10000000;
        for (var i = 0; i < 8; i++) {
            minAABB = min(minAABB, viewSpace[i]);
            maxAABB = max(maxAABB, viewSpace[i]);
        }
        aabb = ComputeAABB(minAABB, maxAABB);
    }
    //Plane minPlane;
    //minPlane.normal = float3(0, 0, -1);
    //minPlane.dist   = -minDepthView;

    GroupMemoryBarrierWithGroupSync();

    // Cull point lights
    if (cs.LocalInvocationIndex < lightIndicesInView[0]) {
        let lightIndex = lightIndicesInView[cs.LocalInvocationIndex + 2];
        let light = pc.lights[lightIndex];

        Sphere s;
        s.pos = mul(float4(light.pos, 1), *pc.view).xyz;
        s.r   = light.radius;

        if (SphereInsideAABB(s, aabb) && SphereInsideFrustum(s, frustum, minDepthView, maxDepthView)) {
            uint currentCount;
            InterlockedAdd(visiblePointLightsCount, 1, currentCount);
            lightIndicesInTiles[offset + currentCount] = lightIndex;
		}
    }

    GroupMemoryBarrierWithGroupSync();

    // Cull spot lights
    float4x4 normalTransform = transpose(*pc.invView);
    if (cs.LocalInvocationIndex < lightIndicesInView[1]) {
        let lightIndex = lightIndicesInView[cs.LocalInvocationIndex + lightIndicesInView[0] + 2];
        let light = pc.lights[lightIndex];

        Cone c;
        c.height = light.radius;
        c.tip = mul(float4(light.pos, 1), *pc.view).xyz;
        c.dir = normalize(mul(light.lightDir, normalTransform).xyz);
        c.r   = tan(acos(light.cutoff)) * light.radius;
    
        if (ConeInsideFrustum(c, frustum, minDepthView, maxDepthView)) {
            uint currentCount;
            InterlockedAdd(visibleSpotLightsCount, 1, currentCount);
            lightIndicesInTiles[offset + visiblePointLightsCount + 1 + currentCount] = lightIndex;
    	}
    }

    GroupMemoryBarrierWithGroupSync();

    if(cs.LocalInvocationIndex == 0) {
        if (visiblePointLightsCount < MAX_VISIBLE_LIGHTS)
            lightIndicesInTiles[offset + visiblePointLightsCount] = -1;

        if (visibleSpotLightsCount < MAX_VISIBLE_LIGHTS)
            lightIndicesInTiles[offset + visiblePointLightsCount + 1 + visibleSpotLightsCount] = -1;
    }
}