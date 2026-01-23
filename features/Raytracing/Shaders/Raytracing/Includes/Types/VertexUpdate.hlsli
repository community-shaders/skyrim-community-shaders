#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

struct VertexUpdateData
{
	uint index;
	uint flags;
	uint vertexCount;
    uint boneOffset;
	float3 bonePivot;
	float boundRadius;
};

#ifdef __cplusplus
static_assert(sizeof(VertexUpdateData) % 4 == 0);
#endif

#endif