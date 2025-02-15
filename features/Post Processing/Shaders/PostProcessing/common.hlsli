#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

float4 KarisAverage(float4 a, float4 b, float4 c, float4 d)
{
	float wa = rcp(1 + Color::RGBToLuminance(a.rgb));
	float wb = rcp(1 + Color::RGBToLuminance(b.rgb));
	float wc = rcp(1 + Color::RGBToLuminance(c.rgb));
	float wd = rcp(1 + Color::RGBToLuminance(d.rgb));
	float wsum = wa + wb + wc + wd;
	return (a * wa + b * wb + c * wc + d * wd) / wsum;
}

// Maybe rewrite as fetch
float4 DownsampleCOD(Texture2D tex, SamplerState samp, float2 uv, float2 out_px_size)
{
	int x, y;

	float4 retval = 0;

	[unroll] for (x = 0; x < 2; ++x)
		[unroll] for (y = 0; y < 2; ++y)
			retval += 0.125 * tex.SampleLevel(samp, uv + (int2(x, y) - .5) * out_px_size, 0);

	// const static float weights[9] = { 0.03125, 0.625, 0.03125, 0.625, 0.125, 0.625, 0.03125, 0.625, 0.03125 };
	// corresponds to (1 << (!x + !y)) * 0.03125 when $x,y \in [-1, 1] \cap \mathbb N$
	[unroll] for (x = -1; x <= 1; ++x)
		[unroll] for (y = -1; y <= 1; ++y)
			retval += (1u << (!x + !y)) * 0.03125 * tex.SampleLevel(samp, uv + int2(x, y) * out_px_size, 0);

	return retval;
}

float4 DownsampleCODFirstMip(Texture2D tex, SamplerState samp, float2 uv, float2 out_px_size)
{
	int x, y;

	float4 retval = 0;
	float4 fetches2x2[4];
	float4 fetches3x3[9];

	[unroll] for (x = 0; x < 2; ++x)
		[unroll] for (y = 0; y < 2; ++y)
			fetches2x2[x * 2 + y] = tex.SampleLevel(samp, uv + (int2(x, y) * 2 - 1) * out_px_size, 0);
	[unroll] for (x = 0; x < 3; ++x)
		[unroll] for (y = 0; y < 3; ++y)
			fetches3x3[x * 3 + y] = tex.SampleLevel(samp, uv + (int2(x, y) - 1) * 2 * out_px_size, 0);

	retval += 0.5 * KarisAverage(fetches2x2[0], fetches2x2[1], fetches2x2[2], fetches2x2[3]);

	[unroll] for (x = 0; x < 2; ++x)
		[unroll] for (y = 0; y < 2; ++y)
			retval += 0.125 * KarisAverage(fetches3x3[x * 3 + y], fetches3x3[(x + 1) * 3 + y], fetches3x3[x * 3 + y + 1], fetches3x3[(x + 1) * 3 + y + 1]);

	return retval;
}

/*
    OpenColorIO
        url:    https://github.com/AcademySoftwareFoundation/OpenColorIO/
        license:
			Copyright Contributors to the OpenColorIO Project.

			Redistribution and use in source and binary forms, with or without
			modification, are permitted provided that the following conditions are
			met:

			* Redistributions of source code must retain the above copyright
			notice, this list of conditions and the following disclaimer.
			* Redistributions in binary form must reproduce the above copyright
			notice, this list of conditions and the following disclaimer in the
			documentation and/or other materials provided with the distribution.
			* Neither the name of the copyright holder nor the names of its
			contributors may be used to endorse or promote products derived from
			this software without specific prior written permission.

			THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
			"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
			LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
			A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
			HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
			SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
			LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
			DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
			THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
			(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
			OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

float3 LogContrast(float3 col, float3 contrast, float3 pivot)
{
	return lerp(pivot, col, contrast);
}

float3 LinearContrast(float3 col, float3 contrast, float3 pivot)
{
	col = col / pivot;
	float3 sgn = sign(col);
	col = pow(abs(col), contrast) * pivot;
	col *= sgn;
	return col;
}

float3 Gamma(float3 col, float3 gamma, float3 black_pivot, float3 white_pivot)
{
	col = col - black_pivot;
	float3 sgn = sign(col);
	float3 range = white_pivot - black_pivot;
	col = col / range;
	col = pow(max(0, col), gamma);
	col = col * sgn * range + black_pivot;
	return col;
}

float3 Saturation(float3 col, float sat)
{
	float luma = Color::RGBToLuminance(col);
	return lerp(luma, col, sat);
}

// https://www.shadertoy.com/view/MdjBRy
float3 HueShift(float3 col, float shift)
{
	float3 P = 0.55735 * dot(0.55735, col);
	float3 U = col - P;
	float3 V = cross(0.55735, U);
	col = U * cos(shift * 6.2832) + V * sin(shift * 6.2832) + P;
	return col;
}

float3 ASC_CDL(float3 col, float3 slope, float3 power, float3 offset)
{
	return Gamma(col * slope + offset, power, 0, 1);
}

////////////////////////////////////////////////////////////////////////

//
// RGB / Full-range YCbCr conversions (ITU-R BT.601)
//
float3 RgbToYCbCr(float3 c)
{
	float Y = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
	float Cb = -0.169 * c.r - 0.331 * c.g + 0.500 * c.b;
	float Cr = 0.500 * c.r - 0.419 * c.g - 0.081 * c.b;
	return float3(Y, Cb, Cr);
}

float3 YCbCrToRgb(float3 c)
{
	float R = c.x + 0.000 * c.y + 1.403 * c.z;
	float G = c.x - 0.344 * c.y - 0.714 * c.z;
	float B = c.x - 1.773 * c.y + 0.000 * c.z;
	return float3(R, G, B);
}

float3 RgbToOklab(float3 c)
{
	float l = 0.4121656120f * c.r + 0.5362752080f * c.g + 0.0514575653f * c.b;
	float m = 0.2118591070f * c.r + 0.6807189584f * c.g + 0.1074065790f * c.b;
	float s = 0.0883097947f * c.r + 0.2818474174f * c.g + 0.6302613616f * c.b;

	float l_ = pow(l, 1. / 3.);
	float m_ = pow(m, 1. / 3.);
	float s_ = pow(s, 1. / 3.);

	float3 labResult;
	labResult.x = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
	labResult.y = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
	labResult.z = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

	return labResult;
}

float3 OklabToRgb(float3 c)
{
	float l_ = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;
	float m_ = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;
	float s_ = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;

	float l = l_ * l_ * l_;
	float m = m_ * m_ * m_;
	float s = s_ * s_ * s_;

	float3 rgbResult;
	rgbResult.r = +4.0767245293f * l - 3.3072168827f * m + 0.2307590544f * s;
	rgbResult.g = -1.2681437731f * l + 2.6093323231f * m - 0.3411344290f * s;
	rgbResult.b = -0.0041119885f * l - 0.7034763098f * m + 1.7068625689f * s;
	return rgbResult;
}

////////////////////////////////////////////////////////////////////////
//
// Functions from PotatoFX
//
////////////////////////////////////////////////////////////////////////

float4 SampleCA(Texture2D tex, SamplerState samp, float2 texcoord, float strength, uint mipLevel)
{
    float3 influence = float3(0.04, 0.0, 0.03);
    float2 CAr = (texcoord - 0.5) * (1.0 - strength * influence.r) + 0.5;
    float2 CAb = (texcoord - 0.5) * (1.0 + strength * influence.b) + 0.5;

    float4 color;
    color.r = tex.SampleLevel(samp, CAr, mipLevel).r;
    color.ga = tex.SampleLevel(samp, texcoord, mipLevel).ga;
    color.b = tex.SampleLevel(samp, CAb, mipLevel).b;

    return color;
}

float4 KawaseBlurDownSample(Texture2D tex, SamplerState samp, uint2 DTid, int scale, float ScreenWidth, float ScreenHeight)
{
    int DownsizeFrom = max(scale, 1);
    float2 texcoord = (floor((DTid.xy / DownsizeFrom)) * DownsizeFrom + DownsizeFrom * 0.5f) / float2(ScreenWidth, ScreenHeight);
    float2 HALF_TEXEL = float2(1.0f / float2(ScreenWidth, ScreenHeight)) * float(DownsizeFrom);

    float2 DirDiag1 = float2(-HALF_TEXEL.x,  HALF_TEXEL.y); // Top left
    float2 DirDiag2 = float2( HALF_TEXEL.x,  HALF_TEXEL.y); // Top right
    float2 DirDiag3 = float2( HALF_TEXEL.x, -HALF_TEXEL.y); // Bottom right
    float2 DirDiag4 = float2(-HALF_TEXEL.x, -HALF_TEXEL.y); // Bottom left

    float4 color = tex.SampleLevel(samp, texcoord, 0) * 4.0f;
    color += tex.SampleLevel(samp, texcoord + DirDiag1, 0);
    color += tex.SampleLevel(samp, texcoord + DirDiag2, 0);
    color += tex.SampleLevel(samp, texcoord + DirDiag3, 0);
    color += tex.SampleLevel(samp, texcoord + DirDiag4, 0);

    return color * 0.125f;
}

float4 KawaseBlurUpSample(Texture2D tex, SamplerState samp, uint2 DTid, int scale, float ScreenWidth, float ScreenHeight)
{
    int Upscale = max(scale, 1);
    float2 texcoord = (floor((DTid.xy / Upscale)) * Upscale + Upscale * 0.5f) / float2(ScreenWidth, ScreenHeight);
    float2 HALF_TEXEL = float2(1.0f / float2(ScreenWidth, ScreenHeight)) * Upscale;

    float2 DirDiag1 = float2(-HALF_TEXEL.x,  HALF_TEXEL.y); // Top left
    float2 DirDiag2 = float2( HALF_TEXEL.x,  HALF_TEXEL.y); // Top right
    float2 DirDiag3 = float2( HALF_TEXEL.x, -HALF_TEXEL.y); // Bottom right
    float2 DirDiag4 = float2(-HALF_TEXEL.x, -HALF_TEXEL.y); // Bottom left
    float2 DirAxis1 = float2(-HALF_TEXEL.x,  0.0f);          // Left
    float2 DirAxis2 = float2( HALF_TEXEL.x,  0.0f);          // Right
    float2 DirAxis3 = float2(0.0f,  HALF_TEXEL.y);           // Top
    float2 DirAxis4 = float2(0.0f, -HALF_TEXEL.y);           // Bottom

    float4 color = 0.0;
    color += tex.SampleLevel(samp, texcoord + DirDiag1, 2);
    color += tex.SampleLevel(samp, texcoord + DirDiag2, 2);
    color += tex.SampleLevel(samp, texcoord + DirDiag3, 2);
    color += tex.SampleLevel(samp, texcoord + DirDiag4, 2);

    color += tex.SampleLevel(samp, texcoord + DirAxis1, 2) * 2.0f;
    color += tex.SampleLevel(samp, texcoord + DirAxis2, 2) * 2.0f;
    color += tex.SampleLevel(samp, texcoord + DirAxis3, 2) * 2.0f;
    color += tex.SampleLevel(samp, texcoord + DirAxis4, 2) * 2.0f;

    return color / 12.0f;
}

float wnoise(float2 uv, float2 d)
{
	float t = float(SharedData::FrameCount % 1000 + 1);
	return frac(sin(dot(uv - 0.5, d) * t) * 143758.5453);
}