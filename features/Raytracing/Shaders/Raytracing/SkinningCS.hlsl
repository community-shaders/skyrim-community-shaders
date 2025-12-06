#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/Types/VertexUpdate.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"

#define MAX_BONES (255)

StructuredBuffer<float3x4> LocalToRoot                  : register(t0, space0);
StructuredBuffer<VertexUpdateData> UpdateData           : register(t1, space0);
StructuredBuffer<float3x4> BoneMatrices                 : register(t2, space0);

//StructuredBuffer<BoneMatrices> MeshBoneMatrices[]     : register(t0, space1);
StructuredBuffer<Skinning> MeshSkinning[]               : register(t0, space1);
StructuredBuffer<Vertex> Vertices[]                     : register(t0, space2);
StructuredBuffer<float4> DynamicVertices[]              : register(t0, space3);

RWStructuredBuffer<Vertex> OutputVertices[]             : register(u0);

#define DYNAMIC (1 << 1)
#define SKINNED (1 << 2)

[numthreads(16, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    float3x4 localToRoot = LocalToRoot[id];
    float3x3 localToRootRot = (float3x3)localToRoot;  
    
    VertexUpdateData updateData = UpdateData[id];

    uint boneMatrixBase = id * MAX_BONES;
    
    /*float3x4 boneMatrices[MAX_BONES];
    for (uint b = 0; b < updateData.bones; b++)
    {
        boneMatrices[b] = BoneMatrices[id * MAX_BONES + b];
    }*/

    uint shapeIndex = NonUniformResourceIndex(updateData.index);
    
    StructuredBuffer<Skinning> skinning = MeshSkinning[shapeIndex];    
    StructuredBuffer<Vertex> vertices = Vertices[shapeIndex];
    StructuredBuffer<float4> dynamicVertices = DynamicVertices[shapeIndex];
    
    RWStructuredBuffer<Vertex> outputVertices = OutputVertices[shapeIndex];
    
    for (uint v = 0; v < updateData.vertexCount; v++)
    {
        float4 dynamicVertex = dynamicVertices[v];
        
        Vertex vertex = vertices[v];
        
        if (updateData.flags & DYNAMIC)
        {
            //vertex.Position = mul(localToRoot, float4(dynamicVertex.xyz, 1.0f));
            vertex.Position = dynamicVertex.xyz;
            vertex.Bitangent = (half3) mul(localToRootRot, half3(dynamicVertex.w, vertex.Bitangent.yz));
        }
   
        if (updateData.flags & SKINNED)
        {
            Skinning vertSkinning = skinning[v];
            
            float3x4 boneMatrix;
            
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
        }        
        
        outputVertices[v] = vertex;
    }
}