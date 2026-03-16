// VR In-Scene Overlay Vertex Shader
// Simple pass-through shader for rendering overlay quad in VR

cbuffer MatrixBuffer : register(b0)
{
	matrix wvp;
};

struct VS_INPUT
{
	float3 pos: POSITION;
	float2 uv: TEXCOORD0;
};

struct PS_INPUT
{
	float4 pos: SV_POSITION;
	float2 uv: TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
	PS_INPUT output;
	output.pos = mul(float4(input.pos, 1.0f), wvp);
	output.uv = input.uv;
	return output;
}
