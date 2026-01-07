#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/Types/VertexUpdate.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"

#define MAX_BONES (255)

cbuffer FrameData : register(b0)
{
    uint ModelCount;
};

RWStructuredBuffer<Vertex> OutputVertices[]             : register(u0);

StructuredBuffer<float3x4> LocalToRoot                  : register(t0, space0);
StructuredBuffer<VertexUpdateData> UpdateData           : register(t1, space0);
StructuredBuffer<float3x4> BoneMatrices                 : register(t2, space0);

//StructuredBuffer<BoneMatrices> MeshBoneMatrices[]     : register(t0, space1);
StructuredBuffer<float4> DynamicVertices[]              : register(t0, space1);
StructuredBuffer<Skinning> MeshSkinning[]               : register(t0, space2);
//StructuredBuffer<Vertex> Vertices[]                     : register(t0, space2);

#define DYNAMIC (1 << 1)
#define SKINNED (1 << 2)

[numthreads(THREAD_SIZE, THREAD_SIZE, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (id.y >= ModelCount)
        return;

    float3x4 localToRoot = LocalToRoot[id.y];
    float3x3 localToRootRot = (float3x3)localToRoot;

    VertexUpdateData updateData = UpdateData[id.y];

    if (id.x >= updateData.vertexCount)
        return;

    uint shapeIndex = NonUniformResourceIndex(updateData.index);

    StructuredBuffer<Skinning> skinning = MeshSkinning[shapeIndex];
    //StructuredBuffer<Vertex> vertices = Vertices[shapeIndex];
    StructuredBuffer<float4> dynamicVertices = DynamicVertices[shapeIndex];

    RWStructuredBuffer<Vertex> outputVertices = OutputVertices[shapeIndex];

    float4 dynamicVertex = dynamicVertices[id.x];

    // This contains the original uploaded vertex
    Vertex vertex = outputVertices[id.x];

    if (updateData.flags & DYNAMIC)
    {
        //vertex.Position = mul(localToRoot, float4(dynamicVertex.xyz, 1.0f));
        vertex.Position = dynamicVertex.xyz;
        vertex.Bitangent = (half3) mul(localToRootRot, half3(dynamicVertex.w, vertex.Bitangent.yz));
    }

    /*if (updateData.flags & SKINNED)
    {
        Skinning vertSkinning = skinning[v];

        float3x4 boneMatrix;

        uint boneMatrixBase = id.y * MAX_BONES;

        [unroll]
        for (uint b = 0; b < 4; b++)
        {
            half weight = vertSkinning.weight[b];
            uint bone = vertSkinning.GetBone(b);

            boneMatrix += BoneMatrices[boneMatrixBase + bone] * weight;
        }

        float3x3 boneMatrixRot = (float3x3)boneMatrix;

        vertex.Position = mul(boneMatrix, float4(vertex.Position, 1.0f));
        vertex.Normal = (half3)mul(boneMatrixRot, vertex.Normal);
        vertex.Tangent = (half3)mul(boneMatrixRot, vertex.Tangent);
        vertex.Bitangent = (half3)mul(boneMatrixRot, vertex.Bitangent);
    }*/

    outputVertices[id.x] = vertex;
}