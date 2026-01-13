#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/Types/VertexUpdate.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"

#define MAX_BONES (255)

RWStructuredBuffer<Vertex> OutputVertices[]             : register(u0);

//StructuredBuffer<float3x4> LocalToRoot                  : register(t0, space0);
StructuredBuffer<VertexUpdateData> UpdateData           : register(t1, space0);
//StructuredBuffer<float3x4> BoneMatrices                 : register(t2, space0);

//StructuredBuffer<BoneMatrices> MeshBoneMatrices[]     : register(t0, space1);
StructuredBuffer<float4> DynamicVertices[]              : register(t0, space1);
StructuredBuffer<Skinning> MeshSkinning[]               : register(t0, space2);
//StructuredBuffer<Vertex> Vertices[]                     : register(t0, space2);

namespace Flags
{
    static const uint16_t Dynamic = (1 << 1);
    static const uint16_t Skinned = (1 << 2); 
}

#if defined(OPTIMIZED_MAPPING)
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 GID : SV_GroupID)
{
    const uint modelIndex = GID.x;
    const uint vertexIndex = GID.y * THREAD_GROUP_SIZE + GTid.x; 
#else
[numthreads(1, THREAD_GROUP_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint modelIndex = DTid.x;
    const uint vertexIndex = DTid.y;    
#endif 
    
    //float3x4 localToRoot = LocalToRoot[modelIndex];
    //float3x3 localToRootRot = (float3x3)localToRoot;

    VertexUpdateData updateData = UpdateData[modelIndex];

    if (vertexIndex >= uint(updateData.vertexCount))
        return;

    uint shapeIndex = NonUniformResourceIndex(updateData.index);

    //StructuredBuffer<Vertex> vertices = Vertices[shapeIndex];

    // This contains the original uploaded vertex
    Vertex vertex = OutputVertices[shapeIndex][vertexIndex];

    if (updateData.flags & Flags::Dynamic)
    {
        float4 dynamicVertex = DynamicVertices[shapeIndex][vertexIndex];      
        
        vertex.Position = mul(updateData.localToRoot, float4(dynamicVertex.xyz, 1.0f));
        //vertex.Bitangent = (half3) mul(localToRootRot, half3(dynamicVertex.w, vertex.Bitangent.yz));
    }
    
    /*if (updateData.flags & Flags::Skinned)
    {
        StructuredBuffer<Skinning> skinning = MeshSkinning[shapeIndex];
    
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

    OutputVertices[shapeIndex][vertexIndex] = vertex;
}