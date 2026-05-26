#include "common.slang"

[[vk::binding(4, 0)]] RWStructuredBuffer<Frustum> tileFrustums;
[[vk::binding(7, 0)]] RWStructuredBuffer<int> lightIndicesInView;

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
bool ConeInsideFrustum(Cone cone, Frustum frustum) {
    for(uint i = 0; i < 6; i++) {
        if(ConeInsidePlane(cone, frustum.planes[i]))
            return false;
    }

    return true;
}


groupshared uint visiblePointLightsCount;
groupshared uint visibleSpotLightsCount;
groupshared float minDepth;
groupshared float maxDepth;
groupshared Frustum frustum;

struct ComputeShaderInput
{
    uint3 WorkGroupID : SV_GroupID;
    uint3 LocalInvocationID : SV_GroupThreadID;
    uint3 GlobalInvocationID : SV_DispatchThreadID;
    uint LocalInvocationIndex : SV_GroupIndex;
};

[shader("compute")]
[numthreads(32, 32, 1)]
void computeMain(ComputeShaderInput cs, uniform PushConstant pc) {
    if (cs.LocalInvocationIndex == 0) {
		visiblePointLightsCount = 0;
        visibleSpotLightsCount  = 0;

        frustum = tileFrustums[pc.sceneInfo.tileCountX * pc.sceneInfo.tileCountY];
        minDepth = -frustum.planes[4].dist;
        maxDepth = frustum.planes[5].dist;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Cull point lights
    if (cs.LocalInvocationIndex < pc.sceneInfo.pointLightCount) {
        let light = pc.lights[cs.LocalInvocationIndex];

        Sphere s;
        s.pos = mul(float4(light.pos, 1), *pc.view).xyz;
        s.r   = light.radius;
    
        if (SphereInsideFrustum(s, frustum, minDepth, maxDepth)) {
            uint currentCount;
            InterlockedAdd(visiblePointLightsCount, 1, currentCount);
            lightIndicesInView[currentCount + 2] = int(cs.LocalInvocationIndex);
		}
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Cull spot lights
	float4x4 normalTransform = transpose(*pc.invView);
    if (cs.LocalInvocationIndex < pc.sceneInfo.spotLightCount) {
        let light = pc.lights[cs.LocalInvocationIndex + pc.sceneInfo.pointLightCount];
    
        Cone c;
        c.height = light.radius;
        c.tip = mul(float4(light.pos, 1), *pc.view).xyz;
        c.dir = normalize(mul(light.lightDir, normalTransform).xyz);
        c.r   = tan(acos(light.cutoff)) * light.radius;
    
        if (ConeInsideFrustum(c, frustum)) {
            uint currentCount;
            InterlockedAdd(visibleSpotLightsCount, 1, currentCount);
            lightIndicesInView[visiblePointLightsCount + currentCount + 2] = int(cs.LocalInvocationIndex + pc.sceneInfo.pointLightCount);
		}
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (cs.LocalInvocationIndex == 0) {
        lightIndicesInView[0] = visiblePointLightsCount;
        lightIndicesInView[1] = visibleSpotLightsCount;
    }
}