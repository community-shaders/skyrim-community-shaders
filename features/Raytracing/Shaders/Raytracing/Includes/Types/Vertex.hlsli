#ifndef VERTEX_HLSI
#define VERTEX_HLSI

#include "Raytracing/Includes/Types.hlsli"

struct Vertex
{
	float3 Position;
	half2p Texcoord0;
	byte4f Normal;
	byte4f Tangent;
	ubyte4f Color;
};

#endif