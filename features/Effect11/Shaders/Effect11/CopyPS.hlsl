Texture2D SourceTexture : register(t0);

cbuffer DitherParams : register(b0)
{
	uint FrameCount;
};

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

uint3 pcg3d(uint3 v)
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	return v;
}

float3 TriDither(float2 screenPos, uint frameCount)
{
	uint3 seed1 = uint3(screenPos, frameCount);
	uint3 seed2 = uint3(screenPos, frameCount + 4729u);
	return (pcg3d(seed1) - pcg3d(seed2)) / float(0xFFFFFFFFu);
}

float4 main(PS_INPUT input) : SV_TARGET
{
	float3 color = SourceTexture.Load(int3(input.pos.xy, 0)).rgb;
	color += TriDither(input.pos.xy, FrameCount) / 255.0;
	return float4(color, 1.0);
}
