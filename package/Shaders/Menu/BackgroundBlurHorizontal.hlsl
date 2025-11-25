// Horizontal Blur Pass Shader
// Part of the BackgroundBlur system - separable Gaussian blur implementation

cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;  // x = 1/width, y = 1/height, z = blur strength, w = unused
    int4   BlurParams; // x = samples, y = unused, z = unused, w = unused
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

float GaussianWeight(float offset)
{
    const float SIGMA = 2.0f;
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (3.14159265f * v);
}

float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    const int samples = min(BlurParams.x, 15);
    const int halfSamples = samples / 2;

    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        float2 sampleCoord = input.TexCoord + float2(i * TexelSize.x, 0.0f);
        float weight = GaussianWeight(float(i));

        if (sampleCoord.x >= 0.0f && sampleCoord.x <= 1.0f)
        {
            result += InputTexture.Sample(LinearSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f)
        result /= totalWeight;

    return result;
}
