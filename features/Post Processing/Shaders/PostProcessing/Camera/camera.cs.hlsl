// Originally from PotatoFX by Gimle Larpes, modified by Jiaye for Community Shaders
/*
MIT License

Copyright (c) 2023 Gimle Larpes

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PostProcessing/common.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

cbuffer CameraCB : register(b1)
{
    // Fisheye
    float FEFoV;
    float FECrop;

    // Chromatic aberration
    float CAStrength;

    // Noise
    float NoiseStrength;
    int NoiseType;
    float2 ScreenSize;
	
	bool UseFE;
}

Texture2D<float4> InputTexture : register(t0);

SamplerState ColorSampler : register(s0);

RWTexture2D<float4> OutputTexture : register(u0);

#define BUFFER_ASPECT_RATIO ScreenSize.x / ScreenSize.y
#define ASPECT_RATIO float2(BUFFER_ASPECT_RATIO, 1.0)
#define EPSILON 1e-6

float2 FishEye(float2 texcoord, float FEFoV, float FECrop)
{
	float2 radiant_vector = texcoord - 0.5;
	float diagonal_length = length(ASPECT_RATIO);
		
	float fov_factor = Math::PI * float(FEFoV)/360.0;

	float fit_fov = sin(atan(tan(fov_factor) * diagonal_length));
	float crop_value = lerp(1.0 + (diagonal_length - 1.0) * cos(fov_factor), diagonal_length, FECrop * pow(abs(sin(fov_factor)), 6.0));//This is stupid and there is a better way.
		
	//Circularize radiant vector and apply cropping
	float2 cn_radiant_vector = 2.0 * radiant_vector * ASPECT_RATIO / crop_value * fit_fov;

	if (length(cn_radiant_vector) < 1.0)
	{
		//Calculate z-coordinate and angle
		float z = sqrt(1.0 - cn_radiant_vector.x*cn_radiant_vector.x - cn_radiant_vector.y*cn_radiant_vector.y);
		float theta = acos(z) / fov_factor;

		float2 d = normalize(cn_radiant_vector);
		texcoord = (theta * d) / (2.0 * ASPECT_RATIO) + 0.5;
	} 

	return texcoord;
}

float3 ClipBlacks(float3 c)
{
    return float3(max(c.r, 0.0), max(c.g, 0.0), max(c.b, 0.0));
}

[numthreads(8, 8, 1)]
void CS_Camera(uint3 DTid : SV_DispatchThreadID)
{
    static const float INVNORM_FACTOR = 0.57735026918962576450914878050196f;  // 1/√3
    static const float2 TEXEL_SIZE = float2(1.0f / ScreenSize.x, 1.0f / ScreenSize.y);
    float2 texcoord = (DTid.xy + 0.5f) * TEXEL_SIZE;
    float2 radiant_vector = texcoord.xy - 0.5;
	float2 texcoord_clean = texcoord.xy;

    ////Effects
	//Fisheye
	if (UseFE)
	{
		texcoord.xy = FishEye(texcoord_clean, FEFoV, FECrop);
	}

	float3 color = InputTexture.SampleLevel(ColorSampler, texcoord, 0).rgb;

    //Chromatic aberration
	[branch]
	if (CAStrength != 0.0)
	{
		color = SampleCA(InputTexture, ColorSampler, texcoord, CAStrength, 0).rgb;
	}

    //Noise
	[branch]
	if (NoiseStrength != 0.0)
	{
		static const float NOISE_CURVE = max(INVNORM_FACTOR * 0.025, 1.0);
		float luminance = Color::RGBToLuminance(color);

        //White noise
		float noise1 = wnoise(texcoord, float2(6.4949, 39.116));
		float noise2 = wnoise(texcoord, float2(19.673, 5.5675));
		float noise3 = wnoise(texcoord, float2(36.578, 26.118));

		//Box-Muller transform
		float r = sqrt(-2.0 * log(noise1 + EPSILON));
		float theta1 = 2.0 * Math::PI * noise2;
		float theta2 = 2.0 * Math::PI * noise3;

        //Sensor sensitivity to color channels: https://www.1stvision.com/cameras/AVT/dataman/ibis5_a_1300_8.pdf
		float3 gauss_noise = float3(r * cos(theta1) * 1.33, r * sin(theta1) * 1.25, r * cos(theta2) * 2.0);
		gauss_noise = (NoiseType == 0) ? gauss_noise.rrr : gauss_noise;

        float weight = (NoiseStrength * NoiseStrength) * NOISE_CURVE / (luminance * (1.0 + rcp(INVNORM_FACTOR)) + 2.0); //Multiply luminance to simulate a wider dynamic range
		color.rgb = ClipBlacks(color.rgb + gauss_noise * weight);
    }

    OutputTexture[DTid.xy] = float4(color, 1.0f);
}