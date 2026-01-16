#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

struct VertexUpdateData
{
	uint index;
	uint flags;
	uint vertexCount;
    uint boneOffset;
#ifndef __cplusplus
	row_major
#endif
	float3x4 localToRoot;
	float3 bonePivot;
	uint pad;
};

#ifdef __cplusplus
static_assert(sizeof(VertexUpdateData) % 4 == 0);
#endif

#endif