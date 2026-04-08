// Local Tonemapping Shaders (Ported from dxvk-remix)
// License: NVIDIA CORPORATION (MIT)

struct LuminanceArgs
{
    float exposure;
    float shadows;
    float highlights;
    uint debugView;
    uint useLegacyACES;
    uint pad1;
    uint pad2;
    uint pad3;
};

struct ExposureWeightArgs
{
    float sigmaSq;
    float offset;
    uint debugView;
    uint padding;
};

struct BlendArgs
{
    float padding[3];
    uint debugView;
};

struct BlendLaplacianArgs
{
    uint2 resolution;
    uint boostLocalContrast;
    uint debugView;
};

struct FinalCombineArgs
{
    float4 mipPixelSize;
    uint2 resolution;
    float exposure;
    uint debugView;
    uint finalizeWithACES;
    uint performSRGBConversion;
    uint pad0;
    uint pad1;
    uint ditherMode;
    uint frameIndex;
    uint useLegacyACES;
    uint pad2;
};

struct MipmapArgs
{
    uint2 resolution;
    float2 texelSize;
};

// https://github.com/Filoppi/PumboAutoHDR/blob/master/Shaders/Pumbo/Color.fxh

// MIT License

// Copyright (c) 2023 Filippo Tarpini

// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// sRGB gamma to linear (scRGB)
float sRGB_to_linear(float color, bool ignoreOutOfGamut /*= false*/)
{
    const float a = 0.055f;

    [flatten]
    if (ignoreOutOfGamut && (color >= 1.f || color <= 0.f))
    {
        // Nothing to do
    }
    else if (color <= 0.04045f)
        color = color / 12.92f;
    else
        color = pow((color + a) / (1.0f + a), 2.4f);

    return color;
}

float3 sRGB_to_linear(float3 colour, bool ignoreOutOfGamut /*= false*/)
{
    return float3(
		sRGB_to_linear(colour.r, ignoreOutOfGamut),
		sRGB_to_linear(colour.g, ignoreOutOfGamut),
		sRGB_to_linear(colour.b, ignoreOutOfGamut));
}

float linear_to_sRGB(float channel)
{
	if (channel <= 0.0031308f)
	{
		channel = channel * 12.92f;
	}
	else
	{
		channel = 1.055f * pow(channel, 1.f / 2.4f) - 0.055f;
	}
	return channel;
}

float3 linear_to_sRGB(float3 Color)
{
    return float3(linear_to_sRGB(Color.r), linear_to_sRGB(Color.g), linear_to_sRGB(Color.b));
}

float3 ACES_Filmic(float3 color)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    
    return (color * ((a * color) + b)) / (color * ((c * color) + d) + e);
}

float3 inv_ACES_Filmic(float3 color)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    
    // Avoid out of gamut colors from breaking the formula
    color = saturate(color);
    
    float3 fixed_numerator = (-d * color) + b;
    float3 variable_numerator_part1 = (d * color) - b;
    float3 variable_numerator = sqrt((variable_numerator_part1 * variable_numerator_part1) - (4.f * e * color * ((c * color) - a)));
    float3 denominator = 2.f * ((c * color) - a);
    float3 result1 = (fixed_numerator + variable_numerator) / denominator;
    float3 result2 = (fixed_numerator - variable_numerator) / denominator;
    color = max(result1, result2);
    return color;
}


float calcBt709Luminance(float3 linearColor)
{
    return dot(linearColor, float3(0.2126, 0.7152, 0.0722));
}

// Shaders
cbuffer CB_Luminance : register(b0) { LuminanceArgs cbLuminance; }
Texture2D<float4> OriginalTexture : register(t0);
RWTexture2D<float4> OutLuminanceTexture : register(u0);

[numthreads(16, 16, 1)]
void CS_Luminance(uint2 threadId : SV_DispatchThreadID)
{
    float avgExposure = cbLuminance.exposure;
    float3 inpColor = OriginalTexture[threadId].xyz;
    
    // Inverse ACES to get back to HDR
    inpColor = inv_ACES_Filmic(sRGB_to_linear(saturate(inpColor)));
    
    inpColor *= avgExposure;
    float highlights = calcBt709Luminance(linear_to_sRGB(ACES_Filmic(inpColor * cbLuminance.highlights)));
    float midtones   = calcBt709Luminance(linear_to_sRGB(ACES_Filmic(inpColor)));
    float shadows    = calcBt709Luminance(linear_to_sRGB(ACES_Filmic(inpColor * cbLuminance.shadows)));
    
    OutLuminanceTexture[threadId] = float4(highlights, midtones, shadows, 1.0);
}

cbuffer CB_ExposureWeight : register(b0) { ExposureWeightArgs cbWeight; }
Texture2D<float4> LuminanceTexture : register(t0);
RWTexture2D<float4> WeightTexture : register(u0);

[numthreads(16, 16, 1)]
void CS_ExposureWeight(uint2 threadId : SV_DispatchThreadID)
{
    float3 diff = LuminanceTexture[threadId].xyz - float3(0.50 + cbWeight.offset, 0.50 + cbWeight.offset, 0.50 + cbWeight.offset);
    float3 weights = exp(-0.5 * diff * diff * cbWeight.sigmaSq);
    weights /= dot(weights, float3(1.0, 1.0, 1.0)) + 0.00001;
    WeightTexture[threadId] = float4(weights, 1.0);
}

cbuffer CB_Blend : register(b0) { BlendArgs cbBlend; }
Texture2D<float4> ExposureTexture : register(t0);
Texture2D<float4> WeightTexture_Blend : register(t1);
RWTexture2D<float4> MipsAssembleTexture : register(u0);

[numthreads(16, 16, 1)]
void CS_Blend(uint2 threadId : SV_DispatchThreadID)
{
    float3 weights = WeightTexture_Blend[threadId].xyz;
    float3 exposures = ExposureTexture[threadId].xyz;
    weights /= dot(weights, float3(1.0, 1.0, 1.0)) + 0.0001;
    MipsAssembleTexture[threadId] = float4(dot(exposures * weights, float3(1.0, 1.0, 1.0)).xxx, 1.0);
}

cbuffer CB_BlendLaplacian : register(b0) { BlendLaplacianArgs cbLaplacian; }
Texture2D<float4> ExposuresTexture_Lap : register(t0);
Texture2D<float4> ExposuresCoarserTexture_Lap : register(t1);
Texture2D<float4> WeightTexture_Lap : register(t2);
Texture2D<float4> AccumulateTexture_Lap : register(t3);
RWTexture2D<float4> OutputTexture_Lap : register(u0);
SamplerState LinearSampler : register(s0);

[numthreads(16, 16, 1)]
void CS_BlendLaplacian(uint2 threadId : SV_DispatchThreadID)
{
    if (any(threadId >= cbLaplacian.resolution)) return;
    float2 vUv = (threadId + 0.5) / float2(cbLaplacian.resolution);

    float accumSoFar = AccumulateTexture_Lap.SampleLevel(LinearSampler, vUv, 0).x;
    float3 laplacians = ExposuresTexture_Lap.SampleLevel(LinearSampler, vUv, 0).xyz - ExposuresCoarserTexture_Lap.SampleLevel(LinearSampler, vUv, 0).xyz;
    float3 weights = WeightTexture_Lap.SampleLevel(LinearSampler, vUv, 0).xyz * (cbLaplacian.boostLocalContrast != 0 ? abs(laplacians) + 0.00001 : float3(1.0, 1.0, 1.0));
    weights /= dot(weights, float3(1.0, 1.0, 1.0)) + 0.00001;
    float laplac = dot(laplacians * weights, float3(1.0, 1.0, 1.0));
    OutputTexture_Lap[threadId] = float4((accumSoFar + laplac).xxx, 1.0);
}

cbuffer CB_FinalCombine : register(b0) { FinalCombineArgs cbFinal; }
Texture2D<float4> OriginalMip0Texture : register(t0);
Texture2D<float4> WeightMip0Texture : register(t1);
Texture2D<float4> OriginalMipTexture : register(t2);
Texture2D<float4> MipAssembleTexture_Final : register(t3);
RWTexture2D<float4> InputOutputTexture_Final : register(u0);

[numthreads(16, 16, 1)]
void CS_FinalCombine(uint2 threadId : SV_DispatchThreadID)
{
    uint width, height;
    InputOutputTexture_Final.GetDimensions(width, height);
    if (threadId.x >= width || threadId.y >= height) return;

    float2 vUv = (threadId + 0.5) / float2(width, height);
    float avgExposure = cbFinal.exposure;
    float3 inpColor = InputOutputTexture_Final[threadId].xyz;
    
    // Inverse ACES to get back to HDR
    inpColor = InverseACESFilmApproximation(gammaToLinear(saturate(inpColor)));
    
    float4 originalColor = float4(inpColor * avgExposure, 1.0);

    float momentX = 0.0;
    float momentY = 0.0;
    float momentX2 = 0.0;
    float momentXY = 0.0;
    float ws = 0.0;
    for (int dy = -1; dy <= 1; dy += 1) {
        for (int dx = -1; dx <= 1; dx += 1) {
            float x = OriginalMipTexture.SampleLevel(LinearSampler, vUv + float2(dx, dy) * cbFinal.mipPixelSize.xy, 0).y;
            float y = MipAssembleTexture_Final.SampleLevel(LinearSampler, vUv + float2(dx, dy) * cbFinal.mipPixelSize.xy, 0).x;
            float w = exp(-0.5 * float(dx*dx + dy*dy) / (0.7*0.7));
            momentX += x * w;
            momentY += y * w;
            momentX2 += x * x * w;
            momentXY += x * y * w;
            ws += w;
        }
    }
    momentX /= ws; momentY /= ws; momentX2 /= ws; momentXY /= ws;
    float A = (momentXY - momentX * momentY) / (max(momentX2 - momentX * momentX, 0.0) + 0.00001);
    float B = momentY - A * momentX;

    float3 texelOriginal = linear_to_sRGB(ACES_Filmic(originalColor.xyz));
    float luminance = calcBt709Luminance(texelOriginal) + 0.00001;
    float finalMultiplier = max(A * luminance + B, 0.0) / luminance;
    float lerpToUnityThreshold = 0.007;
    finalMultiplier = luminance > lerpToUnityThreshold ? finalMultiplier : 
        lerp(1.0, finalMultiplier, (luminance / lerpToUnityThreshold) * (luminance / lerpToUnityThreshold));

    float3 texelFinal = linear_to_sRGB(ACES_Filmic(originalColor.xyz * finalMultiplier));
    InputOutputTexture_Final[threadId] = float4(texelFinal, 1.0);
}

cbuffer CB_Mipmap : register(b0) { MipmapArgs cbMip; }
Texture2D<float4> InMip : register(t0);
RWTexture2D<float4> OutMip : register(u0);

[numthreads(16, 16, 1)]
void CS_GenerateMip(uint2 threadId : SV_DispatchThreadID)
{
    if (any(threadId >= cbMip.resolution)) return;
    float2 vUv = (threadId + 0.5) / float2(cbMip.resolution);
    
    // 5x5 Gaussian
    float weights[5] = { 0.06136, 0.24477, 0.38774, 0.24477, 0.06136 };
    float4 color = 0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            color += InMip.SampleLevel(LinearSampler, vUv + float2(dx, dy) * cbMip.texelSize, 0) * weights[dx+2] * weights[dy+2];
        }
    }
    OutMip[threadId] = color;
}
