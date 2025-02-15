#ifndef __SKINNED_DEPENDENCY_HLSL__
#define __SKINNED_DEPENDENCY_HLSL__

cbuffer PreviousBonesBuffer : register(b9)
{
	float4 PreviousBones[240] : packoffset(c0);
}

cbuffer BonesBuffer : register(b10)
{
	float4 Bones[240] : packoffset(c0);
}

namespace Skinned
{
	float3x4 GetBoneTransformMatrix(float4 bonePositions[240], int4 boneIndices, float3 pivot, float4 boneWeights)
	{
		float3x4 pivotMatrix = transpose(float4x3(0.0.xxx, 0.0.xxx, 0.0.xxx, pivot));

		float3x4 boneMatrix1 =
			float3x4(bonePositions[boneIndices.x], bonePositions[boneIndices.x + 1], bonePositions[boneIndices.x + 2]);
		float3x4 boneMatrix2 =
			float3x4(bonePositions[boneIndices.y], bonePositions[boneIndices.y + 1], bonePositions[boneIndices.y + 2]);
		float3x4 boneMatrix3 =
			float3x4(bonePositions[boneIndices.z], bonePositions[boneIndices.z + 1], bonePositions[boneIndices.z + 2]);
		float3x4 boneMatrix4 =
			float3x4(bonePositions[boneIndices.w], bonePositions[boneIndices.w + 1], bonePositions[boneIndices.w + 2]);

		float3x4 unitMatrix = float3x4(1.0.xxxx, 1.0.xxxx, 1.0.xxxx);
		float3x4 weightMatrix1 = unitMatrix * boneWeights.x;
		float3x4 weightMatrix2 = unitMatrix * boneWeights.y;
		float3x4 weightMatrix3 = unitMatrix * boneWeights.z;
		float3x4 weightMatrix4 = unitMatrix * boneWeights.w;

		return (boneMatrix1 - pivotMatrix) * weightMatrix1 +
		       (boneMatrix2 - pivotMatrix) * weightMatrix2 +
		       (boneMatrix3 - pivotMatrix) * weightMatrix3 +
		       (boneMatrix4 - pivotMatrix) * weightMatrix4;
	}

	float3x3 GetBoneRSMatrix(float4 bonePositions[240], int4 boneIndices, float4 boneWeights)
	{
		float3x3 result;
		for (int rowIndex = 0; rowIndex < 3; ++rowIndex) {
			result[rowIndex] = boneWeights.xxx * bonePositions[boneIndices.x + rowIndex].xyz +
			                   boneWeights.yyy * bonePositions[boneIndices.y + rowIndex].xyz +
			                   boneWeights.zzz * bonePositions[boneIndices.z + rowIndex].xyz +
			                   boneWeights.www * bonePositions[boneIndices.w + rowIndex].xyz;
		}
		return result;
	}
}

#endif  // __SKINNED_DEPENDENCY_HLSL__