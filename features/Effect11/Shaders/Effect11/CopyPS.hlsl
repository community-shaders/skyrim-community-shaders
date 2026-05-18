Texture2D SourceTexture : register(t0);

cbuffer DitherParams : register(b0)
{
	float Timer;
};

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

float3 ScreenSpaceDither(float2 vScreenPos)
{
	float3 vDither = dot(float2(171.0, 231.0), vScreenPos.xy + Timer).xxx;
	vDither.rgb = frac(vDither.rgb / float3(103.0, 71.0, 97.0)) - float3(0.5, 0.5, 0.5);
	return vDither.rgb / 255.0;
}

float4 main(PS_INPUT input) : SV_TARGET
{
	float3 color = SourceTexture.Load(int3(input.pos.xy, 0)).rgb;
	color += ScreenSpaceDither(input.pos.xy);
	return float4(color, 1.0);
}
