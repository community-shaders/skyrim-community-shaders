#ifndef VERTEX_HLSI
#define VERTEX_HLSI

#include "RaytracedGI/Raytracing/Types.hlsli"

struct Vertex
{
	float3 Position;
	half2p Texcoord0;
	byte4f Normal;
	ubyte4f Color;
};

#endif