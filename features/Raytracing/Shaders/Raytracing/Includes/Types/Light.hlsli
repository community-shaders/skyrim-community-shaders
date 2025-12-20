#ifndef LIGHT_HLSL
#define LIGHT_HLSL

struct 
#ifdef __cplusplus
alignas(16)
#endif	
	Light
{
	float3 Vector;
	float Range;
	float3 Color;
	uint16_t Type;
	uint16_t ISL;
};
#ifdef __cplusplus
static_assert(sizeof(Light) % 16 == 0);
#endif

#endif