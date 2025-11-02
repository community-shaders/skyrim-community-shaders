#ifndef LIGHT_HLSI
#define LIGHT_HLSI

struct Light
{
	float3 Vector;
	float Range;
	float3 Color;
	uint Pad;
};

#endif