#include "common.slang"

[[vk::binding(10, 0)]] RWStructuredBuffer<uint> indices;

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
    // First index points to the last meshlet which contains data about indexed geometry
    let meshlet = pc.meshlets[indices[0]];
    DispatchMesh(meshlet.triangleCount, 1, 1, payload);
}

struct VertexOutput
{
    float4 screenPos : SV_Position;
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

    let ID = (WorkGroupID.x * 3);
    let index0 = indices[ID + 0];
    let index1 = indices[ID + 1];
    let index2 = indices[ID + 2];

    let vertex0 = pc.vertices[index0];
    let vertex1 = pc.vertices[index1];
    let vertex2 = pc.vertices[index2];

    let projView = *pc.projView;
    vertices[0].screenPos = mul(float4(vertex0.position, 1.0), projView);
    vertices[1].screenPos = mul(float4(vertex1.position, 1.0), projView);
    vertices[2].screenPos = mul(float4(vertex2.position, 1.0), projView);
}

[[vk::binding(0, 0)]] uniform Sampler2D textures[];
[[vk::binding(2, 0)]] StructuredBuffer<int> lightIndices;

[shader("pixel")]
float4 fragMain()
{
    return float4(1.0, 1.0, 1.0, 1.0);
}