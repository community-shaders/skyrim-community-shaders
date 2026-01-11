#include "Raytracing/Denoiser/SVGF/Common.hlsli"

Texture2D<float4>   InputTexture    : register(t0);
Texture2D<float2>   DepthTexture	: register(t4); // Viewspace Depth in R, Depth Width in G

RWTexture2D<float4> FilteredOutput  : register(u0);

#define GAUSSIAN_RADIUS (1)
#define SPATIAL_RADIUS (2)

float GaussianBlur(int2 id, uint2 screenSize)
{
    float sum = 0.f;
    float kernelSum = 0.f;
    
    const float kernel[2][2] =
    {
        { 1.0 / 4.0, 1.0 / 8.0 },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };
    
    for (int y = -GAUSSIAN_RADIUS; y <= GAUSSIAN_RADIUS; y++)
    {
        for (int x = -GAUSSIAN_RADIUS; x <= GAUSSIAN_RADIUS; x++)
        {
            const int2 p = id + int2(x, y);
            
            if (all(p >= 0) && all(p < screenSize))
            {
                const float k = kernel[abs(x)][abs(y)];
                sum += InputTexture[p].w * k;
                kernelSum += k;                
            }
        }
    }

    return sum / kernelSum;
}

// Spatiotemporal Variance-Guided Filter
[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID)
{
    const uint2 screenSize = Resolution;
    if (DTid.x >= screenSize.x || DTid.y >= screenSize.y)
        return;

    const float2 uv = float2(DTid.xy + 0.5) * ResolutionRcp;

    const float4 inputColor = InputTexture[DTid.xy];
    
    const float2 depthWidthCenter = DepthTexture[DTid.xy].xy;

    /*if (depthCenter <= 0.0f || depthCenter >= 1.0f)
    {
        FilteredOutput[DTid.xy] = inputColor;
        return;
    }*/

    const int2 sDTid = int2(DTid.xy);
    
    float3 normalWS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalWS, roughness);
    roughness = clamp(roughness, 0.001f, 1.0f);

    float luminanceCenter = Color::RGBToLuminance(inputColor.rgb);
    float variance = GaussianBlur(sDTid.xy, screenSize);

    float phiLuminance = Frame.ColorPhi * sqrt(max(VAR_EPSILON, variance));
    float phiNormal = Frame.NormalPhi;
    float phiDepth = Frame.AtrousIterations * depthWidthCenter.y * Frame.DepthPhi;
    
#if defined(SSRT_SPECULAR)
    // Trying to reduce blurriness on glossy surfaces
    phiLuminance *= roughness;
    phiNormal /= max(roughness, 0.05f);
#endif

    float weightSum = 0.f;
    float3 blendedColor = 0;
    
    const float kernelWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };  
    
    for (int y = -SPATIAL_RADIUS; y <= SPATIAL_RADIUS; y++)
    {
        for (int x = -SPATIAL_RADIUS; x <= SPATIAL_RADIUS; x++)
        {
            if (x == 0 && y == 0) continue;
            
            // A-Trous sampling
            int2 samplePos = sDTid + int2(x, y) * Frame.AtrousIterations;

            if (all(samplePos >= 0) && all(samplePos < screenSize))
            {
                float4 sampleColor = InputTexture[samplePos];
                float sampleDepth = DepthTexture[samplePos].x;
                
                if (sampleDepth > 0)
                {
                    float3 sampleNormalWS;
                    float sampleRoughness;
                    GetNormalRoughness(samplePos, sampleNormalWS, sampleRoughness);
    
                    float luminanceP = Color::RGBToLuminance(sampleColor.rgb);
                    
                    float weight = CalculateWeight(depthWidthCenter.x, sampleDepth, phiDepth, normalWS, sampleNormalWS, phiNormal, luminanceCenter, luminanceP, phiLuminance);

                    float kernel = kernelWeights[abs(x)] * kernelWeights[abs(y)];
                    weight *= kernel;
                    
                    blendedColor += sampleColor.rgb * weight;
                    weightSum += weight;
                }
            }
        }
    }
    
    if (weightSum > 0.f)
    {
        blendedColor /= weightSum;
    }
    else
    {
        blendedColor = inputColor.rgb;
    }

    FilteredOutput[DTid.xy] = float4(blendedColor, variance);
}