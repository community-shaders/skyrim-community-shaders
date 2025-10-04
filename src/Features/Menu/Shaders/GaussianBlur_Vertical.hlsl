// Vertical Gaussian Blur Shader
// Based on Unrimp rendering engine's separable blur implementation
// Credits: Christian Ofenberg and the Unrimp project (https://github.com/cofenberg/unrimp)
// License: MIT License
// Used for ImGui background blur effects - Second pass (vertical)

// Uniforms
cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;       // x = 1/width, y = 1/height, z = blur strength, w = unused
    int4   BlurParams;      // x = samples, y = unused, z = unused, w = unused
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_INPUT
{
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Vertex Shader - Full screen triangle
VS_OUTPUT VS_Main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = input.Position;
    output.TexCoord = input.TexCoord;
    return output;
}

// Improved Gaussian weight calculation based on Unrimp's implementation
// Uses proper 2D Gaussian distribution with better normalization
float GaussianWeight(float offset)
{
    const float SIGMA = 0.5f;  // Unrimp's SIGMA value for smooth blur
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (sqrt(2.0f * 3.14159265f) * SIGMA);
}

// Pixel Shader - Vertical Gaussian Blur
// Improved implementation to eliminate scanline artifacts
float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    // Use configurable blur samples (default 7 like Unrimp's SHADOW_MAP_FILTER_SIZE)
    const int samples = min(BlurParams.x, 15); // Cap at 15 for performance
    const int halfSamples = samples / 2;

    // Improved vertical sampling with sub-pixel offset for anti-aliasing
    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        // Add slight sub-pixel jitter to reduce aliasing artifacts
        float offset = float(i) + 0.5f * (float(i % 2) - 0.5f) * 0.1f;
        float2 sampleCoord = input.TexCoord + float2(0.0f, offset * TexelSize.y * TexelSize.z);
        float weight = GaussianWeight(offset);

        // Use clamp addressing to avoid artifacts at borders
        sampleCoord = clamp(sampleCoord, 0.0f, 1.0f);

        float4 sample = InputTexture.Sample(LinearSampler, sampleCoord);
        result += sample * weight;
        totalWeight += weight;
    }

    // Normalize by total weight to maintain brightness
    result /= totalWeight;

    // Apply slight gamma correction to reduce scanline perception
    result.rgb = pow(abs(result.rgb), 0.95f) * sign(result.rgb);

    return result;
}