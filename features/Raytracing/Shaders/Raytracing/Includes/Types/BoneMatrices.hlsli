#ifndef BONE_MATRICES_HLSI
#define BONE_MATRICES_HLSI

#define MAX_BONES 32

struct BoneMatrices
{
	float3x4 matrices[MAX_BONES];
};

#endif