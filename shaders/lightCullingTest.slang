#include "common.slang"

[[vk::binding(0, 0)]] RWStructuredBuffer<Sampler2D> textures;
[[vk::binding(1, 0)]] Sampler2D depth;
[[vk::binding(2, 0)]] RWStructuredBuffer<int> lightIndices;
[[vk::binding(4, 0)]] RWStructuredBuffer<Frustum> tileFrustums;

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

groupshared uint minDepthInt;
groupshared uint maxDepthInt;
groupshared uint visiblePointLightsCount;
groupshared uint visibleSpotLightsCount;
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
    let tileID = cs.WorkGroupID.xy;
    uint index = tileID.y * pc.sceneInfo.tileCountX + tileID.x;
    
    if (cs.LocalInvocationIndex == 0) {
	    minDepthInt = 0xffffffff;
	    maxDepthInt = 0;
		visiblePointLightsCount = 0;
        visibleSpotLightsCount  = 0;
        frustum = tileFrustums[index];
    }

    GroupMemoryBarrierWithGroupSync();

    // Compute min and max depth of tile.
    let uv = cs.GlobalInvocationID.xy / float2(pc.sceneInfo.width, pc.sceneInfo.height);
    let depth    = depth.Sample(uv).x;
    let depthInt = asuint(depth);
    
    InterlockedMax(maxDepthInt, depthInt);
    InterlockedMin(minDepthInt, depthInt);

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

    // Compute tile AABB.
    if (cs.LocalInvocationIndex == 0) {
        let id = cs.WorkGroupID;
        float3 viewSpace[8];
        viewSpace[0] = ScreenToView(float4(id.xy * TILE_SIZE, minDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[1] = ScreenToView(float4(float2(id.x + 1, id.y    ) * TILE_SIZE, minDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[2] = ScreenToView(float4(float2(id.x    , id.y + 1) * TILE_SIZE, minDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[3] = ScreenToView(float4(float2(id.x + 1, id.y + 1) * TILE_SIZE, minDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        
        viewSpace[4] = ScreenToView(float4(id.xy * TILE_SIZE, maxDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[5] = ScreenToView(float4(float2(id.x + 1, id.y    ) * TILE_SIZE, maxDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[6] = ScreenToView(float4(float2(id.x    , id.y + 1) * TILE_SIZE, maxDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        viewSpace[7] = ScreenToView(float4(float2(id.x + 1, id.y + 1) * TILE_SIZE, maxDepth, 1.0f), *pc.sceneInfo, *pc.invProj).xyz;
        
        float3 minAABB =  10000000;
        float3 maxAABB = -10000000;
        for (var i = 0; i < 8; i++) {
            minAABB = min(minAABB, viewSpace[i]);
            maxAABB = max(maxAABB, viewSpace[i]);
        }
        aabb = ComputeAABB(minAABB, maxAABB);
    }

    GroupMemoryBarrierWithGroupSync();

    // Cull point lights.
    uint offset = index * MAX_VISIBLE_LIGHTS;

    if (cs.LocalInvocationIndex < pc.sceneInfo.pointLightCount) {
        let light = pc.lights[cs.LocalInvocationIndex];

        Sphere s;
        s.pos = mul(float4(light.pos, 1), *pc.view).xyz;
        s.r   = light.radius;

        if (SphereInsideAABB(s, aabb) && SphereInsideFrustum(s, frustum, minDepthView, maxDepthView)) {
            uint currentCount;
            InterlockedAdd(visiblePointLightsCount, 1, currentCount);
			lightIndices[offset + currentCount] = int(cs.LocalInvocationIndex);
		}
    }

    GroupMemoryBarrierWithGroupSync();

    // Cull spot lights.
	float4x4 normalTransform = transpose(*pc.invView);
    if (cs.LocalInvocationIndex < pc.sceneInfo.spotLightCount) {
        let light = pc.lights[cs.LocalInvocationIndex + pc.sceneInfo.pointLightCount];
    
        Cone c;
        c.height = light.radius;
        c.tip = mul(float4(light.pos, 1), *pc.view).xyz;
        c.dir = normalize(mul(light.lightDir, normalTransform).xyz);
        c.r   = tan(acos(light.cutoff)) * light.radius;
    
        if (ConeInsideFrustum(c, frustum, minDepthView, maxDepthView)) {
            uint currentCount;
            InterlockedAdd(visibleSpotLightsCount, 1, currentCount);
			lightIndices[offset + visiblePointLightsCount + 1 + currentCount] = int(cs.LocalInvocationIndex + pc.sceneInfo.pointLightCount);
		}
    }

    GroupMemoryBarrierWithGroupSync();

    if(cs.LocalInvocationIndex == 0) {
        if (visiblePointLightsCount < MAX_VISIBLE_LIGHTS)
            lightIndices[offset + visiblePointLightsCount] = -1;
    
        if (visibleSpotLightsCount < MAX_VISIBLE_LIGHTS)
            lightIndices[offset + visiblePointLightsCount + 1 + visibleSpotLightsCount] = -1;
    }
}