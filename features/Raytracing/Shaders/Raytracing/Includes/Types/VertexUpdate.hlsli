#ifndef VERTEX_UPDATE_HLSL
#define VERTEX_UPDATE_HLSL

struct VertexUpdateData
{
	uint16_t index;
	uint16_t flags;
	uint16_t vertexCount;
	uint16_t bones;
};

#ifdef __cplusplus
static_assert(sizeof(VertexUpdateData) % 4 == 0);
#endif


#endif