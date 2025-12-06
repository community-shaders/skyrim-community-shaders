#include "Raytracing/Includes/Types.hlsli"

StructuredBuffer<float3x4> LocalToRoot              : register(t0, space0);
ByteAddressBuffer MeshFlags                         : register(t1, space0);
ByteAddressBuffer VertexCount                       : register(t2, space0);
StructuredBuffer<BoneMatrices> MeshBoneMatrices     : register(t3, space0);

//StructuredBuffer<BoneMatrices> MeshBoneMatrices[] : register(t0, space1);
StructuredBuffer<Skinning> MeshSkinning[]           : register(t0, space1);
StructuredBuffer<Vertex> Vertices[]                 : register(t0, space2);
StructuredBuffer<float4> DynamicVertices[]          : register(t0, space3);

RWStructuredBuffer<Vertex> OutputVertices[]         : register(u0);

#define DYNAMIC (1 << 1)
#define SKINNED (1 << 2)

[numthreads(16, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    float3x4 localToRoot = LocalToRoot[id];
    float3x3 localToRootRot = (float3x3)localToRoot;  
    
    uint meshFlags = MeshFlags.Load(4 * id);
    
    uint vertexCount = VertexCount.Load(4 * id);
    
    //StructuredBuffer<BoneMatrices> boneMatrices = MeshBoneMatrices[id];
    float3x4 boneMatrices[MAX_BONES] = MeshBoneMatrices[id].matrices;   
    StructuredBuffer<Skinning> skinning = MeshSkinning[id];    
    StructuredBuffer<Vertex> vertices = Vertices[id];
    StructuredBuffer<float4> dynamicVertices = DynamicVertices[id];
    
    RWStructuredBuffer<Vertex> outputVertices = OutputVertices[id];
    
    for (uint v = 0; v < vertexCount; v++)
    {
        float4 dynamicVertex = dynamicVertices[v];
        
        Vertex vertex = vertices[v];
        
        if (meshFlags & DYNAMIC)
        {
            vertex.Position = mul(localToRoot, float4(dynamicVertex.xyz, 1.0f));
            vertex.Bitangent = (half3) mul(localToRootRot, half3(dynamicVertex.w, vertex.Bitangent.yz));
        }
   
        if (meshFlags & SKINNED)
        {
            Skinning vertSkinning = skinning[v];
            
            float3x4 boneMatrix;
            
            [unroll]
            for (uint b = 0; b < 4; b++)
            {
                half weight = vertSkinning.weight[b];
                uint bone = vertSkinning.GetBone(b);
                
                boneMatrix += boneMatrices[bone] * weight;
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