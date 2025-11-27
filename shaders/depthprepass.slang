#include "common.slang"

struct TaskShaderOutput {
    uint vertexOffset;
    uint triangleOffset;
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
    float4 position : SV_Position;
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

    let projView = *pc.projView;
    vertices[0].position = mul(float4(vertex0.position, 1.0), projView);
    vertices[1].position = mul(float4(vertex1.position, 1.0), projView);
    vertices[2].position = mul(float4(vertex2.position, 1.0), projView);
}

[shader("pixel")]
void fragMain() {}