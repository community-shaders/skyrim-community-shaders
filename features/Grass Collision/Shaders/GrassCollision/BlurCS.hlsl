#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

cbuffer BlurCB : register(b0)
{
    uint2 textureDimensions;
    float blurRadius;
    uint blurDirection; // 0 = horizontal, 1 = vertical
}

Texture2D<float2> inputTexture : register(t0);
RWTexture2D<float2> outputTexture : register(u0);

SamplerState LinearSampler : register(s0);

// Gaussian weights for blur kernel
static const float GAUSS_WEIGHTS[9] = {
    0.04779, 0.09654, 0.15915, 0.20577, 0.22272,
    0.20577, 0.15915, 0.09654, 0.04779
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= textureDimensions.x || dispatchThreadId.y >= textureDimensions.y)
        return;

    float2 texelSize = 1.0 / float2(textureDimensions);
    float2 uv = (float2(dispatchThreadId.xy) + 0.5) * texelSize;

    float2 result = float2(0, 0);
    float totalWeight = 0.0;

    // 9-tap Gaussian blur
    for (int i = -4; i <= 4; i++)
    {
        float2 sampleUV = uv;

        if (blurDirection == 0) // Horizontal
        {
            sampleUV.x += float(i) * blurRadius * texelSize.x;
        }
        else // Vertical
        {
            sampleUV.y += float(i) * blurRadius * texelSize.y;
        }

        // Clamp to texture bounds
        if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && sampleUV.y >= 0.0 && sampleUV.y <= 1.0)
        {
            float2 sample = inputTexture.SampleLevel(LinearSampler, sampleUV, 0).xy;
            float weight = GAUSS_WEIGHTS[i + 4];

            // Only blur if the sample contains valid collision data (not the default high value)
            if (sample.x < 100000.0 && sample.y < 100000.0)
            {
                result += sample * weight;
                totalWeight += weight;
            }
        }
    }

    // If we have valid samples, use the blurred result, otherwise keep original
    float2 originalSample = inputTexture.SampleLevel(LinearSampler, uv, 0).xy;
    if (totalWeight > 0.0)
    {
        result /= totalWeight;
        // Preserve the original if it's invalid collision data
        if (originalSample.x >= 100000.0 || originalSample.y >= 100000.0)
        {
            result = originalSample;
        }
    }
    else
    {
        result = originalSample;
    }

    outputTexture[dispatchThreadId.xy] = result;
}