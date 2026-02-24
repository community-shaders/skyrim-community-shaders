// VR In-Scene Overlay Pixel Shader
// Samples overlay texture with alpha blending support

Texture2D shaderTexture : register(t0);
SamplerState sampleType : register(s0);

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
	float4 color = shaderTexture.Sample(sampleType, input.uv);
	return color;
}
