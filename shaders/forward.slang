#include "common.slang"

struct TaskShaderOutput {
    uint vertexOffset;
    uint triangleOffset;
    uint materialIndex;
};

groupshared TaskShaderOutput payload;

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain(
    uint3 WorkGroupID: SV_GroupID,
    uniform PushConstant pc
) {
    let ID = WorkGroupID.x;
    let meshlet = pc.meshlets[ID];
    payload.vertexOffset = meshlet.vertexOffset;
    payload.triangleOffset = meshlet.triangleOffset;

    let bound = pc.meshletBounds[ID];
    let view = pc.meshViews[bound.meshID];
    payload.materialIndex = view.material;

    // Frustum culling against the meshlets sphere.
    for (int i = 0; i < 6; i++) {
        let a = pc.vertices[i].position.xyz * bound.sphereCenter;
        if (a.x + a.y + a.z + pc.vertices[i].u <= -bound.sphereRadius) return;
    }

    // Backface culling against the meshlets cone.
    let coneTip = bound.coneTip;
    let coneDir = bound.coneDirection;
    if (dot(normalize(coneTip - pc.camPos.xyz), coneDir) >= bound.coneCutoff && view.flags > 0) return;

    DispatchMesh(meshlet.triangleCount, 1, 1, payload);
}

struct VertexOutput
{
    float4 screenPos : SV_Position;
    float4 viewPos;
    float3 normal;
    float4 color;
    float2 uv;
    uint materialIndex;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain(
    out indices uint3 triangles[1],
    out vertices VertexOutput vertices[3],
    uniform PushConstant pc,
    uint3 WorkGroupID : SV_GroupID
)
{
    uint vertexCount = 3U;
    uint triangleCount = 1U;
    SetMeshOutputCounts(vertexCount, triangleCount);
    triangles[0] = uint3(0, 1, 2);

    let ID = (WorkGroupID.x * 3) + payload.triangleOffset;
    let meshletVert0 = uint(pc.meshletTriangles[ID]);
    let meshletVert1 = uint(pc.meshletTriangles[ID + 1]);
    let meshletVert2 = uint(pc.meshletTriangles[ID + 2]);

    let index0 = pc.meshletVertices[meshletVert0 + payload.vertexOffset];
    let index1 = pc.meshletVertices[meshletVert1 + payload.vertexOffset];
    let index2 = pc.meshletVertices[meshletVert2 + payload.vertexOffset];

    let vertex0 = pc.vertices[index0];
    let vertex1 = pc.vertices[index1];
    let vertex2 = pc.vertices[index2];

    vertices[0].materialIndex = payload.materialIndex;
    vertices[1].materialIndex = payload.materialIndex;
    vertices[2].materialIndex = payload.materialIndex;
    vertices[0].uv = float2(vertex0.u, vertex0.v);
    vertices[1].uv = float2(vertex1.u, vertex1.v);
    vertices[2].uv = float2(vertex2.u, vertex2.v);

    let projView = *pc.projView;
    vertices[0].screenPos = mul(float4(vertex0.position, 1.0), projView);
    vertices[1].screenPos = mul(float4(vertex1.position, 1.0), projView);
    vertices[2].screenPos = mul(float4(vertex2.position, 1.0), projView);
    let view = *pc.view;
    vertices[0].viewPos = mul(float4(vertex0.position, 1.0), view);
    vertices[1].viewPos = mul(float4(vertex1.position, 1.0), view);
    vertices[2].viewPos = mul(float4(vertex2.position, 1.0), view);

    let normalTransform = float3x3(transpose(*pc.invView));
    vertices[0].normal = mul(vertex0.normal, normalTransform);
    vertices[1].normal = mul(vertex1.normal, normalTransform);
    vertices[2].normal = mul(vertex2.normal, normalTransform);
}

[[vk::binding(0, 0)]] uniform Sampler2D textures[];
[[vk::binding(2, 0)]] StructuredBuffer<int> lightIndices;

[shader("pixel")]
float4 fragMain(VertexOutput input, uniform PushConstant pc)
{
    let material = pc.materials[input.materialIndex];
    let diff = textures[material.diffuse].Sample(input.uv);

    var fragment = float3(0.0);
    let N = normalize(input.normal);
    let normalTransform = transpose(*pc.invView);
    let metallicRoughness = textures[material.metallicRoughness].Sample(input.uv);

    uint i = 0;
    uint limit = 0;

    for (limit += pc.sceneInfo.pointLightCount; i < limit; i++) {
        var light = pc.lights[i];
        light.pos = mul(float4(light.pos, 1), *pc.view).xyz;
        fragment += CalcPointLight(light, input.viewPos.xyz, N, diff.xyz, metallicRoughness);
    }

    for (limit += pc.sceneInfo.spotLightCount; i < limit; i++) {
        Light light = pc.lights[i];
        light.pos = mul(float4(light.pos, 1), *pc.view).xyz;
        light.lightDir = mul(light.lightDir, normalTransform);

        fragment += CalcSpotLight(light, input.viewPos.xyz, N, diff.xyz, metallicRoughness);
    }

    for (limit += pc.sceneInfo.dirLightCount; i < limit; i++) {
        var light = pc.lights[i];
        light.lightDir = mul(light.lightDir, normalTransform);
    	fragment += CalcDirLight(light, input.viewPos.xyz, N, diff.xyz, metallicRoughness);
    }

    // Gamma correction.
    fragment = fragment.xyz / (fragment.xyz + float3(1.0));
    fragment = pow(fragment.xyz, float3(1.0 / 2.2));

    // Add emissive to final pixel.
    let emissiveFrag = textures[material.emmisive].Sample(input.uv).xyz;
    fragment = lerp(fragment, emissiveFrag, dot(emissiveFrag, float3(1.0)));
    return float4(fragment, 1.0);
}

float3 CalcDirLight(Light light, float3 V, float3 N, float3 albedo, float4 metallicRoughness) {
    let L = -normalize(light.lightDir.xyz);
    let H = normalize(L - V);

    let radiance = light.color.xyz;

    let NdotH = max(dot(N, H), 0.0);
    let coverage = max(dot(L, N), 0.0);
    let F0 = lerp(float3(0.04), albedo, metallicRoughness.z);

    let NDF = DistributionGGX(NdotH, metallicRoughness.y);
    let G = GeometrySmith(N, -V, L, metallicRoughness.y);
    let kspecular = FresnelSchlick(max(dot(H, -V), 1.0), F0);

    let num = NDF * G * kspecular;
    let dom = 4.0 * max(dot(N, -V), 0.0) * coverage + 0.0001;
    let specular = num / dom;

    let kdiffuse = float3(1.0) - kspecular;

    return (kdiffuse * albedo / PI + specular) * radiance * coverage;
}
float3 CalcSpotLight(Light light, float3 V, float3 N, float3 albedo, float4 metallicRoughness) {
    let L = normalize(light.pos - V);
    let theta = dot(L, -normalize(light.lightDir.xyz));
    if (theta < light.cutoff)
        return float3(0.0);

    let H = normalize(L - V);
    let epsilon = light.innerCutoff - light.cutoff;
    let intensity = clamp((theta - light.cutoff) / epsilon, 0.0, 1.0);

    let normaDist = distance(light.pos, V) / light.radius;
    if (normaDist >= 1.0)
        return float3(0.0);

    let normaDist2 = normaDist * normaDist;
    let attenuation = pow((1 - normaDist2), 2) / (1 + light.falloff * normaDist2);
    let radiance = attenuation * light.color;

    let NdotH = max(dot(N, H), 0.0);
    let coverage = max(dot(L, N), 0.0);
    let F0 = lerp(float3(0.04), albedo, metallicRoughness.z);

    let NDF = DistributionGGX(NdotH, metallicRoughness.y);
    let G = GeometrySmith(N, -V, L, metallicRoughness.y);
    let kspecular = FresnelSchlick(max(dot(H, -V), 1.0), F0);

    let num = NDF * G * kspecular;
    let dom = 4.0 * max(dot(N, -V), 0.0) * coverage + 0.0001;
    let specular = num / dom;

    let kdiffuse = float3(1.0) - kspecular;

    return (kdiffuse * albedo / PI + specular) * radiance * coverage * intensity;
}
float3 CalcPointLight(Light light, float3 V, float3 N, float3 albedo, float4 metallicRoughness) {
    let L = normalize(light.pos - V);
    let H = normalize(L - V);

    let normaDist = distance(light.pos, V) / light.radius;
    if (normaDist >= 1.0)
        return float3(0);

    let normaDist2 = normaDist * normaDist;
    let attenuation = pow((1 - normaDist2), 2) / (1 + light.falloff * normaDist2);
    let radiance = attenuation * light.color;

    let NdotH = max(dot(N, H), 0.0);
    let coverage = max(dot(L, N), 0.0);

    let F0 = lerp(float3(0.04), albedo, metallicRoughness.z);
    let kspecular = FresnelSchlick(max(dot(H, -V), 1.0), F0);

    let NDF = DistributionGGX(NdotH, metallicRoughness.y);
    let G = GeometrySmith(N, -V, L, metallicRoughness.y);

    let num = NDF * G * kspecular;
    let dom = 4.0 * max(dot(N, -V), 0.0) * coverage + 0.0001;
    let specular = num / dom;

    let kdiffuse = float3(1.0) - kspecular;

    return (kdiffuse * albedo / PI + specular) * radiance * coverage;
}