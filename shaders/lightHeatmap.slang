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
    let tileID = int2(input.screenPos.xy / TILE_SIZE);
    let tileIndex = tileID.y * pc.sceneInfo.tileCountX + tileID.x;

    uint offset = tileIndex * MAX_VISIBLE_LIGHTS;
    uint i;
    uint lightsInFrag = 1;
    for (i = 0; i < MAX_VISIBLE_LIGHTS; i++) {
        int lightindex = lightIndices[i + offset];
        if (lightindex == -1) break;
        lightsInFrag++;
    }

    for (i++; i < MAX_VISIBLE_LIGHTS; i++) {
        int lightindex = lightIndices[i + offset];
        if (lightindex == -1) break;
        lightsInFrag++;
    }

    var index = 0U;
    var limit = pc.sceneInfo.pointLightCount;
    index = pc.sceneInfo.spotLightCount;
    limit += pc.sceneInfo.spotLightCount;

    index += pc.sceneInfo.pointLightCount;
    for (limit += pc.sceneInfo.dirLightCount; index < limit; index++) {
        int lightindex = lightIndices[i + offset];
        if (lightindex == -1) break;
        lightsInFrag++;
    }

    let minimum = 0.0f;
    let maximum = float(MAX_VISIBLE_LIGHTS) / 2;
    let ratio   = 2 * (lightsInFrag - minimum) / (maximum);

    let r = max(0, 1 * (ratio - 1));
    let b = max(0, 1 * (1 - ratio));
    let g = 1 - b - r;

    let heatmapFrag = float4(r, g, b, 0.5);
    return heatmapFrag;
}