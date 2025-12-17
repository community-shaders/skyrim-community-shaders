#ifndef VERTEX_HLSL
#define VERTEX_HLSL

#include "Raytracing/Includes/Types/byte4.hlsli"

struct Vertex
{
	float3 Position;
	half2 Texcoord0;
	half3 Normal;
	half3 Tangent;
	ubyte4f Color; // Color before bitangent fixes alignment
	half3 Bitangent;
	uint16_t Pad; // Padding to 4-byte boundary else things break
};
#ifdef __cplusplus
static_assert(sizeof(Vertex) % 4 == 0);
#endif

#endif