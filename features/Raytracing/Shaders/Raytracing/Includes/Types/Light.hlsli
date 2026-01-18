#ifndef LIGHT_HLSL
#define LIGHT_HLSL

#ifndef __cplusplus
namespace LightFlags
{
    static const uint16_t ISL = (1 << 0);
	static const uint16_t LinearLight = (1 << 1);
}
#endif

struct
#ifdef __cplusplus
alignas(16)
#endif
	Light
{
	float3 Vector;
	float Radius;
	float3 Color;
	float InvRadius;
	float FadeZone;
	float SizeBias;
	float Pad0;
	uint16_t Type;
	uint16_t Flags;
};
#ifdef __cplusplus
static_assert(sizeof(Light) % 16 == 0);
#endif

struct
#ifdef __cplusplus
alignas(16)
#endif
	DirectionalLight
{
	float3 Vector;
	float Pad0;
	float3 Color;
	float Pad1;
};
#ifdef __cplusplus
static_assert(sizeof(DirectionalLight) % 16 == 0);
#endif

#endif