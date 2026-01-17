#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/Types/VertexUpdate.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"

#define MAX_BONES (255)

struct BoneMatrix
{
    row_major float3x4 World;
};

RWStructuredBuffer<Vertex> OutputVertices[]             : register(u0);

StructuredBuffer<VertexUpdateData> UpdateData           : register(t0, space0);
StructuredBuffer<BoneMatrix> BoneMatrices               : register(t1, space0);

StructuredBuffer<float4> DynamicVertices[]              : register(t0, space1);

StructuredBuffer<Skinning> MeshSkinning[]               : register(t0, space2);

//StructuredBuffer<Vertex> Vertices[]                     : register(t0, space2);

namespace Flags
{
    static const uint Dynamic = (1 << 1);
    static const uint Skinned = (1 << 2);
}

float3x4 GetBoneTransformMatrix(Skinning skinning, float3 pivot, uint boneOffset)
{
    float3x4 pivotMatrix = transpose(float4x3(0.0.xxx, 0.0.xxx, 0.0.xxx, pivot));

	float3x4 boneMatrix1 = BoneMatrices[boneOffset + skinning.GetBone(0)].World;
	float3x4 boneMatrix2 = BoneMatrices[boneOffset + skinning.GetBone(1)].World;
	float3x4 boneMatrix3 = BoneMatrices[boneOffset + skinning.GetBone(2)].World;
	float3x4 boneMatrix4 = BoneMatrices[boneOffset + skinning.GetBone(3)].World;

	return (boneMatrix1 - pivotMatrix) * skinning.weight[0] +
		    (boneMatrix2 - pivotMatrix) * skinning.weight[1] +
		    (boneMatrix3 - pivotMatrix) * skinning.weight[2] +
		    (boneMatrix4 - pivotMatrix) * skinning.weight[3];
}

float3x3 GetBoneRSMatrix(Skinning skinning, uint boneOffset)
{
    float3x4 boneMatrix1 = BoneMatrices[boneOffset + skinning.GetBone(0)].World;
    float3x4 boneMatrix2 = BoneMatrices[boneOffset + skinning.GetBone(1)].World;
    float3x4 boneMatrix3 = BoneMatrices[boneOffset + skinning.GetBone(2)].World;
    float3x4 boneMatrix4 = BoneMatrices[boneOffset + skinning.GetBone(3)].World;

    float3x3 rs1 = (float3x3)boneMatrix1;
    float3x3 rs2 = (float3x3)boneMatrix2;
    float3x3 rs3 = (float3x3)boneMatrix3;
    float3x3 rs4 = (float3x3)boneMatrix4;

    return rs1 * skinning.weight[0] +
           rs2 * skinning.weight[1] +
           rs3 * skinning.weight[2] +
           rs4 * skinning.weight[3];
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

    VertexUpdateData updateData = UpdateData[modelIndex];

    if (vertexIndex >= updateData.vertexCount)
        return;

    uint shapeIndex = NonUniformResourceIndex(updateData.index);

    // This contains the original uploaded vertex
    Vertex vertex = OutputVertices[shapeIndex][vertexIndex];

    if (updateData.flags & Flags::Dynamic)
    {
        float4 dynamicVertex = DynamicVertices[shapeIndex][vertexIndex];

        vertex.Position = dynamicVertex.xyz;
        //vertex.Tangent = (half3)normalize(float3(dynamicVertex.w, vertex.Tangent.yz));
    }

    if (updateData.flags & Flags::Skinned)
    {
        Skinning skinning = MeshSkinning[shapeIndex][vertexIndex];

        float3x4 boneMatrix = GetBoneTransformMatrix(skinning, updateData.bonePivot, updateData.boneOffset);

        vertex.Position = mul(boneMatrix, float4(vertex.Position, 1.0f));

        if ((updateData.flags & Flags::Dynamic) == 0)
        {
            float3x3 boneMatrixRot = (float3x3)boneMatrix;

            vertex.Normal = (half3) normalize(mul(boneMatrixRot, vertex.Normal));
            vertex.Tangent = (half3) normalize(mul(boneMatrixRot, vertex.Tangent));
            vertex.Bitangent = (half3) normalize(mul(boneMatrixRot, vertex.Bitangent));
        }
    }

    OutputVertices[shapeIndex][vertexIndex] = vertex;
}