#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

struct VertexUpdateData
{
	uint index;
	uint updateFlags;
	uint vertexCount;
    uint boneOffset;
	float3 bonePivot;
	uint shapeFlags;
};

#ifdef __cplusplus
static_assert(sizeof(VertexUpdateData) % 4 == 0);
#endif

#endif