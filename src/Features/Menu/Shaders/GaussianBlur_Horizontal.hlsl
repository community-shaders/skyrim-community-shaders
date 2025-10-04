// Horizontal Gaussian Blur Shader
// Based on Unrimp rendering engine's separable blur implementation
// Used for ImGui background blur effects

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

// Gaussian weight calculation based on Unrimp's implementation
float GaussianWeight(float offset)
{
    const float SIGMA = 0.5f;  // Unrimp's SIGMA value
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (3.14159265f * v);
}

// Pixel Shader - Horizontal Gaussian Blur
float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    
    // Use configurable blur samples (default 7 like Unrimp's SHADOW_MAP_FILTER_SIZE)
    const int samples = min(BlurParams.x, 15); // Cap at 15 for performance
    const int halfSamples = samples / 2;
    
    // Sample horizontally
    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        float2 sampleCoord = input.TexCoord + float2(i * TexelSize.x * TexelSize.z, 0.0f);
        float weight = GaussianWeight(float(i));
        
        // Sample the texture with proper bounds checking
        if (sampleCoord.x >= 0.0f && sampleCoord.x <= 1.0f)
        {
            result += InputTexture.Sample(LinearSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }
    
    // Normalize by total weight to maintain brightness
    if (totalWeight > 0.0f)
    {
        result /= totalWeight;
    }
    
    return result;
}