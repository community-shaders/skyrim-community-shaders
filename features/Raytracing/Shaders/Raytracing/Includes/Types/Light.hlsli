#ifndef LIGHT_HLSI
#define LIGHT_HLSI

struct 
#ifdef __cplusplus
alignas(16)
#endif	
	Light
{
	float3 Vector;
	float Range;
	float3 Color;
	uint Pad;
};
#ifdef __cplusplus
static_assert(sizeof(Light) % 16 == 0);
#endif

#endif