#if !defined(OIT_WRITE_DEPTH)
#define OIT_WRITE_DEPTH 0
#endif

#include "Upscaling/UpscaleVS.hlsl"

#if defined(OIT_BLENDED)
#include "OIT/WBOITResolve.hlsli"
#elif defined(OIT_ROV)
#include "OIT/AOITResolve.hlsli"
#else
#include "OIT/DXAOITResolve.hlsli"
#endif

#define PS_INPUT VS_OUTPUT

struct PS_OUT
{
	float4 Color : SV_Target0;
	float4 Alpha : SV_Target1;
#if OIT_WRITE_DEPTH
	float Depth : SV_Depth;
#endif
};

PS_OUT main(PS_INPUT input)
{
	PS_OUT psout;
	uint2 address = uint2(input.Position.xy);
	float4 color;
	float4 wcolor;
	
#if OIT_WRITE_DEPTH
	OIT_RESOLVE_FUNC(address, color, wcolor, psout.Depth);	
#else
	OIT_RESOLVE_FUNC(address, color, wcolor);
#endif

	color.w = 1.0 - color.w;
	wcolor.w = 1.0 - wcolor.w;
	if (wcolor.w > 0.f)
		wcolor.xyz /= wcolor.w;
	psout.Color = color;
	psout.Alpha = wcolor;
	return psout;
}
