#ifndef VERTEX_HLSI
#define VERTEX_HLSI

#include "Raytracing/Includes/Types.hlsli"

struct Vertex
{
	float3 Position;
	half2 Texcoord0;
	half3 Normal;
	half3 Tangent;
	half3 Bitangent;	
	ubyte4f Color;
};

#endif