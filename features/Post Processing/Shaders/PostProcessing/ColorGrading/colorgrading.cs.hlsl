#include "Common/Color.hlsli"
#include "Common/Math.hlsli"

#include "PostProcessing/common.hlsli"
#include "PostProcessing/aces.hlsli"
#include "PostProcessing/ColourTransforms/GT7ToneMapping.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);

cbuffer ColorCB : register(b1) {
    float4 asccdl[3];
    float4 liftgammagain[3]; // lift，gamma，gain
    float4 saturationHueInOutGamma;
    float4 oklchSaturation;
    float4 oklchColorMixer[7];
	float4 contrast;
	float4 pivot;
	float4 exposureTemperatureTint;
	float4 shadows;
	float4 midtones;
	float4 highlights;
	float4 shadowsHighlightsRange; // shadowBegin, shadowEnd, highlightBegin, highlightEnd

	float4 tonemapParams[2];
	float4 colorSpaceTransform[3];
    float4 invColorSpaceTransform[3];

    // game value
    float4 cinematic; // saturation, brightness, contrast
    float4 fade; // color
    float4 tint; // color

    uint logType;
    uint skipLDR;
    uint enableTonemap;
    uint enableColorSpaceTransform;
};

namespace LogType {
	static const uint ACEScct = (1 << 0);
	static const uint ARRIlogC4 = (1 << 1);
	static const uint SonySLog3 = (1 << 2);
	static const uint Invert = (1 << 3);
};

// https://www.shadertoy.com/view/ss23DD
float3 LiftGammaGain(float3 rgb, float4 lift, float4 gamma, float4 gain)
{
	float4 liftt = 1.0 - pow(1.0 - lift, log2(gain + 1.0));

	float4 gammat = gamma.rgba - float4(0.0, 0.0, 0.0, Color::RGBToLuminance(gamma.rgb));
	float4 gammatTemp = 1.0 + 4.0 * abs(gammat);
	gammat = lerp(gammatTemp, 1.0 / gammatTemp, step(0.0, gammat));

	float3 col = rgb;
	float luma = Color::RGBToLuminance(col);

	col = pow(col, gammat.rgb);
	col *= pow(gain.rgb, gammat.rgb);
	col = max(lerp(2.0 * liftt.rgb, 1.0, col), 0.0);

	luma = pow(luma, gammat.a);
	luma *= pow(gain.a, gammat.a);
	luma = max(lerp(2.0 * liftt.a, 1.0, luma), 0.0);

	col += luma - Color::RGBToLuminance(col);

	return col;
}

float3 OklchSaturation(float3 val)
{
	float3 oklab = RgbToOklab(val);

	float c = length(oklab.yz);
	float h = atan2(oklab.z, oklab.y);

	c = min(0.37, c * oklchSaturation.x);
	c = (1 - pow(abs(1 - c / 0.37), oklchSaturation.y)) * 0.37;
	h += oklchSaturation.z * Math::PI;

	sincos(h, oklab.z, oklab.y);
	oklab.yz *= c;

	return max(0, OklabToRgb(oklab));
}

// mimicking lightroom colour mixer
float3 OklchColourMixer(float3 val)
{
	static const float redHue = 0.08120523664;  //0xff0000

	float3 oklab = RgbToOklab(val);

	float l = oklab.x;
	float c = length(oklab.yz);
	float h = atan2(oklab.z, oklab.y);

	float lerpFactor = (h / (2 * Math::PI) - redHue) * 7;
	int leftHue = floor(lerpFactor);
	lerpFactor = lerpFactor - leftHue;
	leftHue += (leftHue < 0) * 7;
	int rightHue = (leftHue + 1) % 7;
	float effect = saturate(c / 0.37);

	// hue shift
	h = h + lerp(oklchColorMixer[leftHue].x, oklchColorMixer[rightHue].x, lerpFactor) * Math::PI / 4;
	// vibrance
	float c1 = (1 - pow(1 - c / 0.37, oklchColorMixer[leftHue].y)) * 0.37;
	float c2 = (1 - pow(1 - c / 0.37, oklchColorMixer[rightHue].y)) * 0.37;
	c = lerp(c1, c2, lerpFactor);
	// brightness
	l = l + lerp(oklchColorMixer[leftHue].z, oklchColorMixer[rightHue].z, lerpFactor) * effect;

	oklab.x = l;
	sincos(h, oklab.z, oklab.y);
	oklab.yz *= c;

	return max(0, OklabToRgb(oklab));
}

float3 ShadowsMidtonesHighlights(float3 color, float3 shadowsColor, float3 midtonesColor, float3 highlightsColor, float shadowBegin, float shadowEnd, float highlightBegin, float highlightEnd)
{
    float luma = Color::RGBToLuminance(color);
    
    float shadowWeight = 1.0 - smoothstep(shadowBegin, shadowEnd, luma);
    
    float highlightWeight = smoothstep(highlightBegin, highlightEnd, luma);
    
    float midtoneWeight = 1.0 - shadowWeight - highlightWeight;

    float3 result = color * shadowsColor * shadowWeight
                  +	color * midtonesColor * midtoneWeight
                  +	color * highlightsColor * highlightWeight;

    return result;
}

float2 IlluminantChromaticity(float temp)
{
	temp *= 1.4388 / 1.438;
	float x = temp <= 7000 ? 0.244063 + (0.09911e3 + (2.9678e6 - 4.6070e9 / temp) / temp) / temp : 0.237040 + (0.24748e3 + (1.9018e6 - 2.0064e9 / temp) / temp) / temp;
	float y = -3 * x * x + 2.87 * x - 0.275;
	return float2(x, y);
}

// [McCamy 1992, "Correlated color temperature as an explicit function of chromaticity coordinates"]
float2 PlanckianIsothermal(float temp, float tint)
{
	float u = (0.860117757f + 1.54118254e-4f * temp + 1.28641212e-7f * temp * temp) / (1.0f + 8.42420235e-4f * temp + 7.08145163e-7f * temp * temp);
	float v = (0.317398726f + 4.22806245e-5f * temp + 4.20481691e-8f * temp * temp) / (1.0f - 2.89741816e-5f * temp + 1.61456053e-7f * temp * temp);
	float ud = (-1.13758118e9f - 1.91615621e6f * temp - 1.53177f * temp * temp) / Square(1.41213984e6f + 1189.62f * temp + temp * temp);
	float vd = (1.97471536e9f - 705674.0f * temp - 308.607f * temp * temp) / Square(6.19363586e6f - 179.456f * temp + temp * temp);
	float2 uvd = normalize(float2(u, v));
	u += -uvd.y * tint * 0.05;
	v +=  uvd.x * tint * 0.05;
	float x = 3 * u / (2 * u - 8 * v + 4);
	float y = 2 * v / (2 * u - 8 * v + 4);
	return float2(x, y);
}

float3 WhiteBalance(float3 linearColor)
{
	float2 srcWhiteDaylight = IlluminantChromaticity(exposureTemperatureTint.y);
	float2 srcWhitePlankian = PlanckianLocusChromaticity(exposureTemperatureTint.y);

	float2 srcWhite = exposureTemperatureTint.y < 4000 ? srcWhitePlankian : srcWhiteDaylight;
	float2 d65White = float2(0.31270, 0.32900);

	float2 isothermal = PlanckianIsothermal(exposureTemperatureTint.y, exposureTemperatureTint.z) - srcWhitePlankian;
	srcWhite += isothermal;

	float3x3 whiteBalance = ChromaticAdaptation(srcWhite, d65White);
	whiteBalance = mul(XYZ_2_sRGB_MAT, mul(whiteBalance, sRGB_2_XYZ_MAT));

	return mul(whiteBalance, linearColor);
}

float3 LogToLinear(float3 logColor)
{
    const float linearRange = 14.0f;
    const float linearGrey = 0.18f;
    const float exposureGrey = 444.0f;
    return exp2((logColor - exposureGrey / 1023.0) * linearRange) * linearGrey;
}

float3 LinearToLog(float3 linearColor)
{
    const float linearRange = 14.0f;
    const float linearGrey = 0.18f;
    const float exposureGrey = 444.0f;
    return saturate(log2(linearColor) / linearRange - log2(linearGrey) / linearRange + exposureGrey / 1023.0f);
}

/////////////////////////////////////////////////////////////////////////////////

/*
    tizian/tonemapper
        url:    https://github.com/tizian/tonemapper
        license:
            The MIT License (MIT)

            Copyright (c) 2022 Tizian Zeltner

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

float3 Reinhard(float3 val)
{
	val *= tonemapParams[0].x;
	float luma = Color::RGBToLuminance(val);
	float lumaOut = luma / (1 + luma);
	val = val / (luma + 1e-10) * lumaOut;
	val = saturate(val);
	return val;
}

float3 ReinhardExt(float3 val)
{
	val *= tonemapParams[0].x;
	float luma = Color::RGBToLuminance(val);
	float lumaOut = luma * (1 + luma / (tonemapParams[0].y * tonemapParams[0].y)) / (1 + luma);
	val = val / (luma + 1e-10) * lumaOut;
	val = saturate(val);
	return val;
}

float3 HejlBurgessDawsonFilmic(float3 val)
{
	val *= tonemapParams[0].x;
	val = max(0, val - 0.004);
	val = (val * (6.2 * val + .5)) / (val * (6.2 * val + 1.7) + 0.06);
	val = pow(saturate(val), 2.2);
	return val;
}

float3 AldridgeFilmic(float3 val)
{
	val *= tonemapParams[0].x;
	float tmp = 2.0 * tonemapParams[0].y;
	val = val + (tmp - val) * clamp(tmp - val, 0.0, 1.0) * (0.25 / tonemapParams[0].y) - tonemapParams[0].y;
	val = (val * (6.2 * val + 0.5)) / (val * (6.2 * val + 1.7) + 0.06);
	val = pow(saturate(val), 2.2);
	return val;
}

float3 AcesHill(float3 val)
{
	static const float3x3 g_sRGBToACEScg = float3x3(
		0.613117812906440, 0.341181995855625, 0.045787344282337,
		0.069934082307513, 0.918103037508582, 0.011932775530201,
		0.020462992637737, 0.106768663382511, 0.872715910619442);
	static const float3x3 g_ACEScgToSRGB = float3x3(
		1.704887331049502, -0.624157274479025, -0.080886773895704,
		-0.129520935348888, 1.138399326040076, -0.008779241755018,
		-0.024127059936902, -0.124620612286390, 1.148822109913262);

	val *= tonemapParams[0].x;

	val = mul(g_sRGBToACEScg, val);
	float3 a = val * (val + 0.0245786f) - 0.000090537f;
	float3 b = val * (0.983729f * val + 0.4329510f) + 0.238081f;
	val = a / b;
	val = mul(g_ACEScgToSRGB, val);

	val = saturate(val);

	return val;
}

float3 AcesNarkowicz(float3 val)
{
	val *= tonemapParams[0].x;

	static const float A = 2.51;
	static const float B = 0.03;
	static const float C = 2.43;
	static const float D = 0.59;
	static const float E = 0.14;
	val *= 0.6;
	val = (val * (A * val + B)) / (val * (C * val + D) + E);
	val = saturate(val);
	return val;
}

float3 AcesGuy(float3 val)
{
	val *= tonemapParams[0].x;
	val = val / (val + 0.155f) * 1.019;

	val = pow(saturate(val), 2.2);
	return val;
}

float3 LottesFilmic(float3 val)
{
	val *= tonemapParams[0].x;
	float a = tonemapParams[0].y,
		  d = tonemapParams[0].z,
		  b = (-pow(tonemapParams[1].x, a) + pow(tonemapParams[0].w, a) * tonemapParams[1].y) /
	          ((pow(tonemapParams[0].w, a * d) - pow(tonemapParams[1].x, a * d)) * tonemapParams[1].y),
		  c = (pow(tonemapParams[0].w, a * d) * pow(tonemapParams[1].x, a) - pow(tonemapParams[0].w, a) * pow(tonemapParams[1].x, a * d) * tonemapParams[1].y) /
	          ((pow(tonemapParams[0].w, a * d) - pow(tonemapParams[1].x, a * d)) * tonemapParams[1].y);

	val = pow(val, a) / (pow(val, a * d) * b + c);
	val = saturate(val);
	return val;
}

float DayCurve(float x, float k)
{
	const float b = tonemapParams[0].y;
	const float w = tonemapParams[0].z;
	const float c = tonemapParams[0].w;
	const float s = tonemapParams[1].x;
	const float t = tonemapParams[1].y;

	if (x < c) {
		return k * (1.0 - t) * (x - b) / (c - (1.0 - t) * b - t * x);
	} else {
		return (1.0 - k) * (x - c) / (s * x + (1.0 - s) * w - c) + k;
	}
}

float3 DayFilmic(float3 val)
{
	const float b = tonemapParams[0].y;
	const float w = tonemapParams[0].z;
	const float c = tonemapParams[0].w;
	const float s = tonemapParams[1].x;
	const float t = tonemapParams[1].y;

	val *= tonemapParams[0].x;
	float k = (1.0 - t) * (c - b) / ((1.0 - s) * (w - c) + (1.0 - t) * (c - b));
	val = float3(DayCurve(val.r, k), DayCurve(val.g, k), DayCurve(val.b, k));

	val = saturate(val);
	return val;
}

float3 UchimuraFilmic(float3 val)
{
	const float P = tonemapParams[0].y;
	const float a = tonemapParams[0].z;
	const float m = tonemapParams[0].w;
	const float l = tonemapParams[1].x;
	const float c = tonemapParams[1].y;
	const float b = tonemapParams[1].z;

	val *= tonemapParams[0].x;

	float l0 = ((P - m) * l) / a,
		  S0 = m + l0,
		  S1 = m + a * l0,
		  C2 = (a * P) / (P - S1),
		  CP = -C2 / P;

	float3 w0 = 1.0 - smoothstep(0.0, m, val),
		   w2 = step(m + l0, val),
		   w1 = 1.0 - w0 - w2;

	float3 T = m * pow(val / m, c) + b,           // toe
		L = m + a * (val - m),                    // linear
		S = P - (P - S1) * exp(CP * (val - S0));  // shoulder

	val = T * w0 + L * w1 + S * w2;

	val = saturate(val);
	return val;
}

/*  AgX Reference:
 *  AgX by longbool https://www.shadertoy.com/view/dtSGD1
 *  AgX Minimal by bwrensch https://www.shadertoy.com/view/cd3XWr
 *  Fork AgX Minima troy_s 342 by troy_s https://www.shadertoy.com/view/mdcSDH
 */

// Mean error^2: 3.6705141e-06
float3 AgxDefaultContrastApprox5(float3 x)
{
	float3 x2 = x * x;
	float3 x4 = x2 * x2;

	return +15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x +
	       0.4298 * x2 + 0.1191 * x - 0.00232;
}

// Mean error^2: 1.85907662e-06
float3 AgxDefaultContrastApprox6(float3 x)
{
	float3 x2 = x * x;
	float3 x4 = x2 * x2;

	return -17.86 * x4 * x2 * x + 78.01 * x4 * x2 - 126.7 * x4 * x + 92.06 * x4 -
	       28.72 * x2 * x + 4.361 * x2 - 0.1718 * x + 0.002857;
}

float3 Agx(float3 val)
{
	const float3x3 agx_mat = transpose(
		float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
			0.0784335999999992, 0.878468636469772, 0.0784336,
			0.0792237451477643, 0.0791661274605434, 0.879142973793104));

	const float min_ev = -12.47393f;
	const float max_ev = 4.026069f;

	// Input transform
	val = mul(agx_mat, val);

	// Log2 space encoding
	val = clamp(log2(val), min_ev, max_ev);
	val = (val - min_ev) / (max_ev - min_ev);

	// Apply sigmoid function approximation
	val = AgxDefaultContrastApprox6(val);

	return val;
}

float3 AgxEotf(float3 val)
{
	const float3x3 agx_mat_inv = transpose(
		float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
			-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
			-0.0990297440797205, -0.0989611768448433, 1.15107367264116));

	// Undo input transform
	val = mul(agx_mat_inv, val);

	// sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
	// NOTE: We're linearizing the output here. Comment/adjust when
	// *not* using a sRGB render target
	val = pow(saturate(val), 2.2);

	return val;
}

float3 AgxMinimal(float3 val)
{
	val *= tonemapParams[0].x;

	val = Agx(val);
	val = ASC_CDL(val, tonemapParams[0].y, tonemapParams[0].z, tonemapParams[0].w);
	val = Saturation(val, tonemapParams[1].x);
	val = AgxEotf(val);

	return val;
}

// src: https://github.com/ltmx/Melon-Tonemapper
// GPL-3.0 license
float3 MelonHueShift(float3 In)
{
	float A = max(In.x, In.y);
	return float3(A, max(A, In.z), In.z);
}

float3 MelonTonemap(float3 color)
{
	color *= tonemapParams[0].r;

	// remaps the colors to [0-1] range
	// tested to be as close ti ACES contrast levels as possible
	color = pow(color, float3(1.56, 1.56, 1.56));
	color = color / (color + 0.84);

	// governs the transition to white for high color intensities
	float factor = max(color.r, max(color.g, color.b)) * 0.15;  // multiply by 0.15 to get a similar look to ACES
	factor = factor / (factor + 1);                             // remaps the factor to [0-1] range
	factor *= factor;                                           // smooths the transition to white

	// shift the hue for high intensities (for a more pleasing look).
	color = lerp(color, MelonHueShift(color), factor);   // can be removed for more neutral colors
	color = lerp(color, float3(1.0, 1.0, 1.0), factor);  // shift to white for high intensities

	// clamp to [0-1] range
	return clamp(color, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
}

/* 
    EmbarkStudios/kajiya
        url:    https://github.com/EmbarkStudios/kajiya	
        license:
			Copyright (c) 2019 Embark Studios

			Permission is hereby granted, free of charge, to any
			person obtaining a copy of this software and associated
			documentation files (the "Software"), to deal in the
			Software without restriction, including without
			limitation the rights to use, copy, modify, merge,
			publish, distribute, sublicense, and/or sell copies of
			the Software, and to permit persons to whom the Software
			is furnished to do so, subject to the following
			conditions:

			The above copyright notice and this permission notice
			shall be included in all copies or substantial portions
			of the Software.

			THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
			ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
			TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
			PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
			SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
			CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
			OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
			IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
			DEALINGS IN THE SOFTWARE.
*/

float KajiyaCurve(float v)
{
	return 1.0 - exp(-v);
}

float3 KajiyaCurve(float3 v)
{
	return 1.0 - exp(-v);
}

float3 KajiyaTonemap(float3 col)
{
	col *= tonemapParams[0].r;

	float3 ycbcr = RgbToYCbCr(col);

	float bt = KajiyaCurve(length(ycbcr.yz) * 2.4);
	float desat = max((bt - 0.7) * 0.8, 0);
	desat = desat * desat;

	float3 desat_col = lerp(col, ycbcr.x, desat);

	float tm_luma = KajiyaCurve(ycbcr.x);
	float3 tm0 = col * max(tm_luma / max(Color::RGBToLuminance(col), 1e-5), 0);
	float final_mult = 0.97;
	float3 tm1 = KajiyaCurve(desat_col);

	return lerp(tm0, tm1, bt * bt) * final_mult;
}

float3 GT7ToneMapping(float3 color)
{
    color *= tonemapParams[0].x;
	if (tonemapParams[0].y == 0)
		color = GT7ToneMappingSDR(color);
	else
		color = GT7ToneMappingHDR(color, tonemapParams[0].z);
    return color;
}

////////////////////////////////////////////////////////////////////////

// Linear to Log
float3 ACEScct(float3 linearColor, bool inverse)
{
	const float a = 10.5402377416545;
	const float b = 0.0729055341958355;
	const float cutoff = 0.0078125;
	const float cutoff2 = 0.155251141552511;

	float3 cct = linearColor;

	if (!inverse) {
		for (int i = 0; i < 3; i++) {
			if (linearColor[i] > cutoff) {
				cct[i] = (log2(linearColor[i]) + 9.72) / 17.52;
			} else {
				cct[i] = linearColor[i] * a + b;
			}
		}
	} else {
		for (int i = 0; i < 3; i++) {
			if (linearColor[i] >= cutoff2) {
				cct[i] = pow(2, linearColor[i] * 17.52 - 9.72);
			} else {
				cct[i] = (linearColor[i] - b) / a;
			}
		}
	}

	return cct;
}

float3 ARRIlogC4(float3 linearColor, bool inverse)
{
	const float a = 2231.8263;
	const float b = 0.9071359;
	const float c = 0.0928641;
	const float s = 0.6816768;
	const float t = -0.0180570;

	float3 logColor = linearColor;

	if (!inverse) {
		for (int i = 0; i < 3; i++) {
			if (linearColor[i] > t) {
				logColor[i] = (log2(linearColor[i] * a + 64) - 6) / 14 * b + c;
			} else {
				logColor[i] = (linearColor[i] - t) / s;
			}
		}
	} else {
		for (int i = 0; i < 3; i++) {
			if (linearColor[i] > 0) {
				logColor[i] = (pow(2, 14 * (linearColor[i] - c) / b + 6) - 64) / a;
			} else {
				logColor[i] = linearColor[i] * s + t;
			}
		}
	}

	return logColor;
}

float3 SonySLog3(float3 linearColor, bool inverse)
{
	float3 logColor = linearColor;

	if (!inverse) {
		for (int i = 0; i < 3; i++) {
			if (linearColor[i] > 0.0112500) {
				logColor[i] = (420.0 + log10((linearColor[i] + 0.01) / 0.19) * 261.5) / 1023.0;
			} else {
				logColor[i] = (linearColor[i] * (171.2102946929 - 95.0) / 0.0112500 + 95.0) / 1023.0;
			}
		}
	} else {
		for (int i = 0; i < 3; i++) {
			if (linearColor[i] > 0.1712102946929 / 1023.0) {
				logColor[i] = pow(10, (linearColor[i] * 1023.0 - 420.0) / 261.5) * 0.19 - 0.01;
			} else {
				logColor[i] = (linearColor[i] * 1023.0 - 95.0) * 0.0112500 / (171.2102946929 - 95.0);
			}
		}
	}

	return logColor;
}

float3 LinearToLogSpace(float3 val, uint logType)
{
	float3 logColor = val;
	if (logType & LogType::ACEScct)
		logColor = ACEScct(val, false);
	else if (logType & LogType::ARRIlogC4)
		logColor = ARRIlogC4(val, false);
	else if (logType & LogType::SonySLog3)
		logColor = SonySLog3(val, false);
	return logColor;
}

float3 LogToLinearSpace(float3 val, uint logType)
{
    float3 linearColor = val;
    if (logType & LogType::ACEScct)
        linearColor = ACEScct(val, true);
    else if (logType & LogType::ARRIlogC4)
        linearColor = ARRIlogC4(val, true);
    else if (logType & LogType::SonySLog3)
        linearColor = SonySLog3(val, true);
    return linearColor;
}

////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID) {
	float3 color = pow(abs(TexColor[DTid].xyz), saturationHueInOutGamma.z) * cinematic.y;

	// Color space transform (preferably sRGB to ACEScg)
    if (enableColorSpaceTransform) {
		const float3x3 colorSpaceTransformMat = float3x3(colorSpaceTransform[0].xyz, colorSpaceTransform[1].xyz, colorSpaceTransform[2].xyz);
        color = mul(colorSpaceTransformMat, color);
    }

	// HDR
	// Exposure/White Balance
	{
		color *= exposureTemperatureTint.x;
		color = WhiteBalance(color);
	}

    // Log
	if (logType)
    	color = LinearToLogSpace(color, logType); // Preferably ACEScct

    // ASC CDL
    color = ASC_CDL(color, asccdl[0].xyz, asccdl[1].xyz, asccdl[2].xyz);

	// Saturation and Hue
	color = Saturation(color, saturationHueInOutGamma.x * cinematic.x);
	color = HueShift(color, saturationHueInOutGamma.y);

	// Shadows Midtones Highlights
	color = ShadowsMidtonesHighlights(color, shadows.xyz, midtones.xyz, highlights.xyz, shadowsHighlightsRange.x, shadowsHighlightsRange.y, shadowsHighlightsRange.z, shadowsHighlightsRange.w);

	// Contrast
	color = LinearContrast(color, contrast.xyz, pivot.xyz);

    if (logType & LogType::Invert) {
        color = LogToLinearSpace(color, logType);
    }

    // Tonemap
    if (enableTonemap) {
        color = TONEMAP_FUNC(color);
    }

	// Inverse color space transform
    if (enableColorSpaceTransform) {
		const float3x3 invColorSpaceTransformMat = float3x3(invColorSpaceTransform[0].xyz, invColorSpaceTransform[1].xyz, invColorSpaceTransform[2].xyz);
        color = mul(invColorSpaceTransformMat, color);
    }

	// LDR
	if (!skipLDR) {
		// Lift Gamma Gain
		color = LiftGammaGain(color, liftgammagain[0].gbar, liftgammagain[1].gbar, liftgammagain[2].gbar);

		// Oklch Saturation
		color = OklchSaturation(color);

		// Oklch Colour Mixer
		color = OklchColourMixer(color);
	}
	
    // Game tint
    float luma = Color::RGBToLuminance(color);
    color = lerp(color, luma * tint.xyz, tint.w);

    color = LinearContrast(color, cinematic.z, 0.18);

	color = pow(abs(color), saturationHueInOutGamma.w);

    // Game fade
    color = lerp(color, fade.xyz, fade.w);

	RWTexOut[DTid] = float4(color, 1);
}