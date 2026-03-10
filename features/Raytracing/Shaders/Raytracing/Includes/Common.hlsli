#ifndef COMMON_HLSLI
#define COMMON_HLSLI

cbuffer ScreenData : register(b0)
{
    uint2 Resolution;
    uint2 DynamicResolution;
};

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

#endif