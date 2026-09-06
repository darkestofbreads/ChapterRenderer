#include "common.slang"

struct TaskShaderOutput {
    uint dummy;
};

groupshared TaskShaderOutput payload;

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain(
    uint3 WorkGroupID: SV_GroupID,
    uniform PushConstant pc
) {
    DispatchMesh(1, 1, 1, payload);
}

struct VertexOutput
{
    float4 screenPos : SV_Position;
    float3 normal;
    float2 uv;
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

    vertices[0].screenPos = float4(-1, -1, 0, 1);
    vertices[0].screenPos = float4(-1, 3, 0, 1);
    vertices[0].screenPos = float4(3, -1, 0, 1);

    vertices[0].uv = float2(0, 0);
    vertices[0].uv = float2(0, 2);
    vertices[0].uv = float2(2, 0);

    let normalTransform = float3x3(transpose(*pc.invView));
    var normal = mul(float3(1), normalTransform);
    vertices[0].normal = normal;
    vertices[1].normal = normal;
    vertices[2].normal = normal;
}

[[vk::binding(0, 0)]] uniform Sampler2D textures[];
[[vk::binding(2, 0)]] StructuredBuffer<int> lightIndices;
[[vk::binding(5, 0)]] Sampler2D dirShadowMap;

struct mat4{
    float4x4 mat;
};
[[vk::binding(6, 0)]] ConstantBuffer<mat4> dirShadowTrans;
[[vk::binding(9, 0)]] RaytracingAccelerationStructure accelerationStruct;

[shader("pixel")]
float4 fragMain(VertexOutput input, uniform PushConstant pc)
{
    let inShadow = anyHitRayQuery(input.screenPos.xyz, input.normal);

    if (inShadow)
        return float4(1, 1, 1, 1.0);
    else
        return float4(0);
}

static const float EPSILON = 0.01;
bool anyHitRayQuery(float3 position, float3 direction) {
    RayDesc desc;
    desc.Origin = position;
    desc.Direction = normalize(direction);
    desc.TMax = 1e4;
    desc.TMin = EPSILON;

    let rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(accelerationStruct, rayFlags, 0xFF, desc);

    while(query.Proceed()) {
        query.CommitNonOpaqueTriangleHit();
    }

    let hit = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
    return hit;
}