#if defined(DISPLAY_DEPTH)
#	include "Common/DummyVSTexCoord.hlsl"
#else
#	include "Common/DummyVS.hlsl"
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState DepthSampler : register(s0);
SamplerState uintStencilSampler : register(s1);

Texture2D<float4> DepthTex : register(t0);
Texture2D<uint4> uintStencilTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 Color : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(DISPLAY_DEPTH)
	float depth;
	if (1e-5 < Color.z) {
		uint2 dimensions;
		uint numberOfLevels;
		uintStencilTex.GetDimensions(0, dimensions.x, dimensions.y, numberOfLevels);
		float3 location = float3(input.TexCoord * dimensions, 0);
		depth = uintStencilTex.Load(location).x;
	} else {
		depth = DepthTex.SampleLevel(DepthSampler, input.TexCoord, 0).x;
	}
	float screenDepth = saturate((-Color.x + depth) / (Color.y - Color.x));
	psout.Color.xyz = (screenDepth * -2 + 3) * (screenDepth * screenDepth);
	psout.Color.w = 1;
#	else
	psout.Color = Color;
#	endif

	return psout;
}
#endif
