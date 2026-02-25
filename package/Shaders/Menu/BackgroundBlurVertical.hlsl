// Vertical Blur Pass Shader
// Part of the BackgroundBlur system - separable Gaussian blur implementation

cbuffer BlurBuffer : register(b0)
{
	float4 TexelSize;  // x = 1/width, y = 1/height, z = blur strength, w = unused
	int4 BlurParams;   // x = samples, y = unused, z = unused, w = unused
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
	output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
	output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
	output.Position.y = -output.Position.y;
	return output;
}

// Precomputed normalized Gaussian weights (sigma = 2.0)
static const float WEIGHTS[8] = {
	0.1760327f,  // offset 0 (center)
	0.1658591f,  // offset ±1
	0.1403215f,  // offset ±2
	0.1069852f,  // offset ±3
	0.0732894f,  // offset ±4
	0.0451904f,  // offset ±5
	0.0248657f,  // offset ±6
	0.0122423f   // offset ±7
};

float4 PS_Main(VS_OUTPUT input) :
	SV_TARGET
{
	const int samples = min(BlurParams.x, 15);
	const int halfSamples = samples / 2;

	// Compute normalization factor for actual weights used
	float weightSum = WEIGHTS[0];
	[unroll(7)] for (int j = 1; j <= halfSamples; ++j)
	{
		weightSum += 2.0f * WEIGHTS[min(j, 7)];
	}
	const float normalization = 1.0f / weightSum;

	// Sample center pixel
	float4 result = InputTexture.Sample(LinearSampler, input.TexCoord) * (WEIGHTS[0] * normalization);

	// Sample symmetric pairs
	[unroll(7)] for (int i = 1; i <= halfSamples; ++i)
	{
		float weight = WEIGHTS[min(i, 7)] * normalization;
		float offset = i * TexelSize.y;

		result += InputTexture.Sample(LinearSampler, input.TexCoord + float2(0.0f, offset)) * weight;
		result += InputTexture.Sample(LinearSampler, input.TexCoord - float2(0.0f, offset)) * weight;
	}

	return result;
}
