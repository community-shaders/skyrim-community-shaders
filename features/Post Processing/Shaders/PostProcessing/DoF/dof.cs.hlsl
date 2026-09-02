////////////////////////////////////////////////////////////////////////////////////////////////////
// Modified by Jiaye
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Cinematic Depth of Field shader, using scatter-as-gather for ReShade 3.x+
// By Frans Bouma, aka Otis / Infuse Project (Otis_Inf)
// https://fransbouma.com
//
// This shader has been released under the following license:
//
// Copyright (c) 2018-2022 Frans Bouma
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Original shader version history:
// 16-aug-2023:	   v1.2.10: Added Cone Overlap support so the HDR conversion first desaturates the colors so channels with a high value don't
//                          exponentially boost to irrealistic values. Contributed by MartyMcFly.
// 26-jun-2023:	   v1.2.9:  Found a way to compensate for edges on close to in-focus geometry shimmering through which were otherwise only removable with the NearFarDistanceCompensation added
//                          in the previous version
// 13-jun-2023:    v1.2.8:  Added the NearFarDistanceCompensation slider for compensating hard edges on geometry that's out of focus but close to the in-focus plane
// 24-jan-2023:    v1.2.7:  Added custom shape support for bokeh highlights. The included shapes were created by Moyevka, Murchalloo, K-putt and others.
// 11-nov-2022:    v1.2.6:  Added bokeh sharpening.
// 28-mar-2022:    v1.2.5:  Made the pre-blur pass optional, as it's not really needed anymore for qualities higher than 4 and reasonable blur values.
// 15-mar-2022:    v1.2.4:  Corrected the LDR to HDR and HDR to LDR conversion functions so they now apply proper gamma correct and boost, so hue shifts are limited now as long
//                          as the highlight boost is kept <= 1
//                          Added Gamma factor for advanced highlight tweaking.
// 11-mar-2022:    v1.2.3:  Changed the sampling stages to use full HDR so there's no more back/forth calculations to SDR along the way. Highlight boost is now
//                          better and upper range has been cranked up.
// 26-feb-2022:    v1.2.2:  Made the highlight boost also be able to go to -1 to dim highlights a bit in bright scenes.
// 22-feb-2022:    v1.2.1:  Removed highlight amplification and properly implemented reinhard-esk de/re-tonemapping for proper highlight calculations. Thanks Marty McFly for the tips.
//                          (1.2.1) small adjustment, added a boost for the highlights which could help in dimly lit scenes. Based on simple levels math.
// 01-jan-2021:    v1.1.19: Corrected PS_PostSmoothing2AndFocusing's signature as it contained a redundant argument which caused warnings in newer versions of reshade.
// 23-oct-2020:    v1.1.18: Near-plane bleed blurred the unblurred far plane which leads to artifacts around edges in some cases. This has been rolled back to the earlier versions of
//                          using the blurred far plane (if any). Also added mirroring to the samplers so edges of the screen aren't blurring darker into the result but should be much smoother.
// 26-mar-2020:    v1.1.17: FreeStyle support added (not yet ansel superres compatible). Fixed issue with far plane highlight causing near plane edge pixels getting highlighted.
// 15-mar-2020:    v1.1.16: Dithering added for low-luma areas to avoid banding. (Contributed by Prod80)
// 03-feb-2020:    v1.1.15: Experimental near plane edge blur improvements.
// 04-oct-2019:    v1.1.14: Fine-tuning of near plane blur using smaller tiles.
// 23-jun-2019:    v1.1.13: Cleanup of highlight code, reimplementing of luma boost / highlightblending. Removal of unnecessary controls.
// 13-jun-2019:    v1.1.12: Bugfix in maxColor blending in near/far blur: no more dirty edges on large highlighted areas.
// 10-jun-2019:	   v1.1.11: Added new weight calculation, added near-plane highlight normalization.
// 25-may-2019:	   v1.1.10: Added white boost/correction in gathering passes to have lower-intensity highlights become less prominent.
//							Added further weight adjustment tweaks. Changed highlight defaults to utilize code changed in 1.1.9/1.1.10
// 24-may-2019:		v1.1.9: Better near-plane bleed mask. Better far plane pixel weights so more samples get accepted.
// 02-mar-2019: 	v1.1.8: Added anamorphic bokeh support, so bokehs now get stretched and rotated based on the distance from the center of the screen, with various tweaks.
// 08-jan-2019:		v1.1.7: Added 9-tap tent filter as described in [Jimenez2014) for mitigating undersampling. Implementation is from KinoBokeh (see credits below).
// 02-jan-2019:		v1.1.6: When near plane max blur is set to 0, the original fragment is now used in the near plane instead of the half-res pixel.
// 19-dec-2018:		v1.1.5: Added far plane highlight normalizing for non-gained highlights. Added tooltip for reshade v4.x
// 14-dec-2018:		v1.1.4: Far plane weight calculation tweaked a bit as near-focus plane elements could lead to hard edges which looked ugly. Highlight far plane
//							adjustments have been reworked because of this.
// 10-dec-2018:		v1.1.3: Removed averaging pass for CoC values as it resulted in noticeable wrong CoC values around edges in some TAA using games. The net result
//							was minimal anyway.
// 10-nov-2018:		v1.1.2: Near plane bugfix: tile gatherer should collect min CoC, not average of min CoC: now ends of narrow lines are properly handled too.
// 30-oct-2018:		v1.1.1: Near plane bugfix for high resolutions: it's now blurring resolution independently. Highlight bleed fix in near focus.
// 21-oct-2018:		v1.1.0: Far plane weights adjustment, half-res with upscale combiner for performance, new highlights implementation, fixed
//							pre-blur highlight smoothing.
// 10-oct-2018:		v1.0.8: Improved, tile-based near-plane bleed, optimizations, far-plane large CoC bleed limitation, Highlight dimming, fixed in-focus
// 						    bleed with post-smooth blur, fixed highlight edges, fixed pre-blur.
// 21-sep-2018:		v1.0.7: Better near-plane bleed. Optimized near plane CoC storage so less reads are needed.
//							Corrected post-blur bleed. Corrected near plane highlight bleed. Overall micro-optimizations.
// 04-sep-2018:		v1.0.6: Small fix for DX9 and autofocus.
// 17-aug-2018:		v1.0.5: Much better highlighting, higher range for manual focus
// 12-aug-2018:		v1.0.4: Finetuned the workaround for d3d9 to only affect reshade 3.4 or lower.
//							Finetuned the near highlight extrapolation a bit. Removed highlight threshold as it ruined the blur
// 10-aug-2018:		v1.0.3: Daodan's crosshair code added.
// 09-aug-2018:		v1.0.2: Added workaround for d3d9 glitch in reshade 3.4.
// 08-aug-2018:		v1.0.1: namespace addition for samplers/textures.
// 08-aug-2018:		v1.0.0: beta. Feature complete.
//
////////////////////////////////////////////////////////////////////////////////////////////////////
// Additional credits:
// Reinhard de/retonemapping for highlighting information thanks to Marty McFly.
// Gaussian blur code based on the Gaussian blur ReShade shader by Ioxa
// Thanks to Daodan for the crosshair code in the focus helper.
// 9 tap tent filter is from KinoBokeh Copyright (C) 2015 Keijiro Takahashi. MIT licensed. See file below for details.
//       Ref:  https://github.com/keijiro/KinoBokeh/blob/master/Assets/Kino/Bokeh/Shader/Composition.cginc
// Thanks to Prod80 for contributing dithering in combiner to avoid banding in low-luma blurred areas.
////////////////////////////////////////////////////////////////////////////////////////////////////
// References:
//
// [Lee2008]		Sungkil Lee, Gerard Jounghyun Kim, and Seungmoon Choi: Real-Time Depth-of-Field Rendering Using Point Splatting
//					on Per-Pixel Layers.
//					https://pdfs.semanticscholar.org/80f6/f40fe971eddc810c3c86fca6fdfe5c0fdd76.pdf
//
// [Jimenez2014]	Jorge Jimenez, Sledgehammer Games: Next generation post processing in Call of Duty Advanced Warfare, SIGGRAPH2014
//					http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
//
// [Nilsson2012]	Filip Nilsson: Implementing realistic depth of field in OpenGL.
//					http://fileadmin.cs.lth.se/cs/education/edan35/lectures/12dof.pdf
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Common/Color.hlsli"
#include "Common/Game.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

#if defined(GATHER_RING_COUNT)
#	include "PostProcessing/DoF/dof_gather_kernel.hlsli"
#endif

RWTexture2D<float4> RWTexOut : register(u0);
RWTexture2D<float> RWFocus : register(u1);
RWTexture2D<float> RWTexCoC : register(u2);
RWTexture2D<float4> RWTexCoCTile : register(u3);

SamplerState LinearSampler : register(s0);

Texture2D<float4> TexColor : register(t0);
Texture2D<float> TexPreviousFocus : register(t1);
Texture2D<float> DepthTexture : register(t2);
Texture2D<float> TexCoCInput : register(t3);  // full res CoC
// t4 was the half-resolution near-mask blur. Near reach is now carried by t11, leaving this slot free.
Texture2D<float4> TexFarBlur : register(t5);
Texture2D<float4> TexNearBlur : register(t6);
Texture2D<float4> TexPostSmoothInput : register(t7);
Texture2D<float4> TexBokehShape : register(t8);
Texture2D<float> TexCoCHalf : register(t9);           // half res CoC (bilateral downsample of t3)
Texture2D<float4> TexCoCTile : register(t10);         // per tile (min, max) signed CoC
Texture2D<float4> TexCoCTileDilated : register(t11);  // (min, max, propagated near reach in pixels, unused)
Texture2D<float4> TexGatherColor1 : register(t12);    // quarter res
Texture2D<float4> TexGatherColor2 : register(t13);    // eighth res
Texture2D<float4> TexGatherColor3 : register(t14);    // sixteenth res
Texture2D<float> TexGatherCoC1 : register(t15);
Texture2D<float> TexGatherCoC2 : register(t16);
Texture2D<float> TexGatherCoC3 : register(t17);
Texture2D<float> TexReduceCoCInput : register(t18);
// xy = shaped sample position, z = radial accumulation weight, w = canonical disc distance.
// Keeping w separate is essential for polygon corners: shaped xy may extend beyond unit radius,
// but its intersection distance still belongs to the same canonical gather ring.
StructuredBuffer<float4> BokehSamples : register(t19);

cbuffer DoFCB : register(b1)
{
	float TransitionSpeed;
	float2 FocusCoord;
	float ManualFocusPlane;
	float FocalLength;
	float FNumber;
	float FarPlaneMaxBlur;
	float NearPlaneMaxBlur;
	float BlurQuality;
	float NearFarDistanceCompensation;
	float BokehBusyFactor;
	float HighlightBoost;
	float PostBlurSmoothing;
	uint HighlightShape;
	float HighlightShapeRotationAngle;
	float PetzvalStrength;
	uint AutoFocus;
	float MaxNearCoCRadius;
	float MaxFarCoCRadius;
	uint TileDilateRadius;  // in tiles
	uint2 CoCTileDim;
	uint2 HalfResDim;
	uint BokehMode;                // 0=procedural, 1=custom texture
	float CustomShapeRadiusScale;  // normalises the custom aperture to the area of a unit circle
	float BokehMaxRadius;
	float NearMaxReachPx;
	uint BokehBladeCount;
	float BokehBladeRoundness;
	float ProceduralBokehAreaScale;
	uint Padding;
};

// Sensor width the FocalLength control is expressed for (35mm full frame).
#define SENSOR_WIDTH_MM 36.0f

// One CoC tile covers exactly one gather thread group: 8x8 half res pixels == 16x16 full res pixels.
// Because of that a gather group can take a *group uniform* (scalar) early out branch from a single
// tile fetch indexed by SV_GroupID, which is where most of the win comes from.
#define COC_TILE_SIZE_HALFRES 8
#define COC_TILE_SIZE_FULLRES 16

// --------------------------------------------------------------------------------------------
// CoC units
//
// A CoC value in this shader is a *signed blur disc radius expressed as a fraction of the screen
// width*: negative = near field (in front of the focal plane), positive = far field.
// Keeping the radius in horizontal viewport units makes every threshold below expressible in
// pixels, which is what the gather kernel actually operates in.
// --------------------------------------------------------------------------------------------
static const float cocToPixels = SharedData::BufferDim.x;    // CoC fraction -> full-res pixels
static const float onePixelInCoC = SharedData::BufferDim.z;  // "less than a pixel of blur" == in focus

// Near CoC radius (in pixels) at which the near field layer becomes fully opaque.
static const float nearFullOpacityPixels = 8.0f;
// A tile whose CoC spread is below this fraction of its max needs no depth layer resolving, so the
// compatibility gather can drop a ring.
static const float fastGatherCoCError = 0.05f;

struct FocusInfo
{
	float2 texcoord;
	float focusDepth;  // in KM, as stored in the 1x1 focus texture
	float focusDepthInM;
	float focusDepthInMM;
};

float GetDepth(float2 uv)
{
	float depth = DepthTexture.SampleLevel(LinearSampler, uv, 0);
	depth = SharedData::GetScreenDepth(depth) * GAME_UNIT_TO_M * 0.001f;  // in KM
	return max(depth, 1e-6);
}

float PreviousFocus()
{
	return TexPreviousFocus[uint2(0, 0)].x;
}

void FillFocusInfoData(inout FocusInfo toFill)
{
	// The 1x1 focus texture holds the focus distance in KM (see CS_UpdateFocus / GetDepth).
	toFill.focusDepth = PreviousFocus();
	toFill.focusDepthInM = toFill.focusDepth * 1000.0;      // km -> m
	toFill.focusDepthInMM = toFill.focusDepthInM * 1000.0;  // m -> mm
}

// Gets the tap from the shape pointed at with the shapeSampler specified, over the angle specified, from the distance of the center in shapeRingDistance
// Returns in rgb the shape sample, and in a the luma.
float4 GetShapeTap(float angle, float shapeRingDistance)
{
	float2 pointOffsetForShape = 0.f;

	// we have to add 270 degrees to the custom angle, because it's scatter via gather, so a pixel that has to show the top of our shape is *above*
	// the highlight, and the angle has to be 270 degrees to hit it (as sampling the highlight *below it* is what makes it brighter).
	sincos(angle + (Math::TAU * HighlightShapeRotationAngle) + (Math::TAU * 0.75f), pointOffsetForShape.x, pointOffsetForShape.y);
	pointOffsetForShape.y *= -1.0f;
	float2 shapeTapCoords = float2((shapeRingDistance * pointOffsetForShape) + 0.5f);  // shapeRingDistance is [0, 0.5] so no need to multiply with 0.5 again
	float4 shapeTap = TexBokehShape.SampleLevel(LinearSampler, shapeTapCoords, 0);
	shapeTap.a = Color::RGBToLuminance(shapeTap.rgb);
	return shapeTap;
}

float CalculateBlurDiscSize(FocusInfo focusInfo)
{
	float pixelDepth = GetDepth(focusInfo.texcoord);
	float pixelDepthInM = pixelDepth * 1000.0;  // in meter

	// CoC (blur disc DIAMETER on the sensor, in mm) based on [Lee2008]:
	//     CoC = ((f*f) / N) / (S1 - f) * (|Z - S1| / Z)
	// where f = FocalLength (mm), N = FNumber, S1 = focus distance, Z = pixel depth.
	// f and S1 must be in the SAME unit for the (S1 - f) term, so S1 has to be in mm.
	// The (|Z - S1| / Z) term is dimensionless and can stay in meters.
	float focalPlaneOffsetInMM = max(focusInfo.focusDepthInMM - FocalLength, 1e-3f);
	float cocDiameterInMM = (((FocalLength * FocalLength) / FNumber) / focalPlaneOffsetInMM) *
	                        (abs(pixelDepthInM - focusInfo.focusDepthInM) / max(pixelDepthInM, 1e-6f));

	// sensor-space diameter (mm) -> screen-space radius (fraction of the screen width)
	float cocRadius = (0.5f * cocDiameterInMM) * (1.0f / SENSOR_WIDTH_MM);

	// Clamp the kernel so an extreme focus setup can never blow up the gather.
	// Apply separate foreground/background safety limits.
	bool isNearField = pixelDepth < focusInfo.focusDepth;
	cocRadius = min(cocRadius, isNearField ? MaxNearCoCRadius : MaxFarCoCRadius);

	return isNearField ? -cocRadius : cocRadius;
}

float3 AccentuateWhites(float3 fragment)
{
	// apply small tow to the incoming fragment, so the whitepoint gets slightly lower than max.
	// We don't need to de-tonemap since we are under HDR.
	return fragment / (HighlightBoost > 0.f ? max((1.001 - (HighlightBoost * fragment)), 0.001) : 1.0f);
}

// returns 2 vectors, (x,y) are up vector, (z,w) are right vector.
// In: pixelVector which is the current pixel converted into a vector where (0,0) is the center of the screen.
float2 ApplyPetzvalMorph(float2 pointOffset, float2 texcoord)
{
	float2 centeredUV = texcoord;
	float2 fromCenter = centeredUV - 0.5f;
	float distanceFromCenter = length(fromCenter);
	float radius = saturate(distanceFromCenter * 2.0f);
	if (PetzvalStrength <= 0.001f || distanceFromCenter <= 0.0001f)
		return pointOffset;

	float2 radialAxis = fromCenter / distanceFromCenter;
	float2 tangentialAxis = float2(-radialAxis.y, radialAxis.x);
	float radialComponent = dot(pointOffset, radialAxis);
	float tangentialComponent = dot(pointOffset, tangentialAxis);
	float petzvalAmount = PetzvalStrength * radius * radius * 1.35f;
	float tangentialScale = 1.0f + petzvalAmount;
	float radialScale = rcp(tangentialScale);

	return radialAxis * (radialComponent * radialScale) + tangentialAxis * (tangentialComponent * tangentialScale);
}

// Scatter-as-gather intersection test: how much of the sample's blur disc covers this fragment.
// Both arguments are in full-res PIXELS, so the +0.5 is the usual half-pixel anti-aliasing term
float CalculateSampleWeight(float sampleRadiusInPixels, float ringDistanceInPixels)
{
	return saturate(sampleRadiusInPixels - (ringDistanceInPixels * NearFarDistanceCompensation) + 0.5);
}

// Clamps a texel coordinate to the buffer.
int2 ClampToBuffer(int2 coord)
{
	return clamp(coord, int2(0, 0), int2(SharedData::BufferDim.xy) - 1);
}

int2 ClampToHalfRes(int2 coord)
{
	return clamp(coord, int2(0, 0), int2(HalfResDim) - 1);
}

// Fetches the (min, max) signed CoC of the tile a gather group lives in. `Gid` is SV_GroupID, which
// maps 1:1 onto the tile grid, so the result is group uniform and the branches using it are scalar.
float2 LoadTileCoCMinMax(Texture2D<float4> tiles, uint2 Gid)
{
	return tiles[min(Gid, CoCTileDim - 1)].xy;
}

float4 PerformFullFragmentGaussianBlur(Texture2D source, float2 texcoord, uint2 DTid, float2 offsetWeight)
{
	float offset[6] = { 0.0, 1.4584295168, 3.40398480678, 5.3518057801, 7.302940716, 9.2581597095 };
	float weight[6] = { 0.13298, 0.23227575, 0.1353261595, 0.0511557427, 0.01253922, 0.0019913644 };

	float coc = TexCoCInput[DTid].r;
	float4 fragment = source[DTid];
	float fragmentLuma = Color::RGBToLuminance(fragment.rgb);
	float4 originalFragment = fragment;
	float absoluteCoC = abs(coc);

	if (absoluteCoC < onePixelInCoC || PostBlurSmoothing < 0.01 || fragmentLuma < 0.3) {
		// in focus or postblur smoothing isn't enabled or not really a highlight, ignore
		return fragment;
	}

	fragment *= weight[0];
	float2 factorToUse = offsetWeight * PostBlurSmoothing;

	for (int i = 1; i < 6; ++i) {
		float2 coordOffset = factorToUse * offset[i];
		float weightSample = weight[i];
		float sampleCoC = TexCoCInput.SampleLevel(LinearSampler, texcoord + coordOffset, 0).r;
		float maskFactor = abs(sampleCoC) < onePixelInCoC;

		fragment += (originalFragment * maskFactor * weightSample) +
		            (source.SampleLevel(LinearSampler, texcoord + coordOffset, 0) * (1 - maskFactor) * weightSample);

		sampleCoC = TexCoCInput.SampleLevel(LinearSampler, texcoord - coordOffset, 0).r;
		maskFactor = abs(sampleCoC) < onePixelInCoC;

		fragment += (originalFragment * maskFactor * weightSample) +
		            (source.SampleLevel(LinearSampler, texcoord - coordOffset, 0) * (1 - maskFactor) * weightSample);
	}
	return fragment;
}

[numthreads(1, 1, 1)] void CS_UpdateFocus(uint2 DTid : SV_DispatchThreadID) {
	float depth = AutoFocus ? GetDepth(FocusCoord) : ManualFocusPlane;
	float previousFocus = TexPreviousFocus[uint2(0, 0)];
	RWFocus[DTid] = lerp(previousFocus, depth, TransitionSpeed);
}

	[numthreads(8, 8, 1)] void CS_CalculateCoC(uint2 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= (uint)SharedData::BufferDim.x || DTid.y >= (uint)SharedData::BufferDim.y)
		return;

	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	FocusInfo focusInfo;
	focusInfo.texcoord = uv;
	FillFocusInfoData(focusInfo);

	float coc = CalculateBlurDiscSize(focusInfo);
	RWTexCoC[DTid] = coc;
}

// Flattens the full res CoC into one (min, max) per 16x16 pixel tile. Uses GatherRed so each
// iteration pulls 4 texels; the whole pass reads the full res CoC exactly once.
[numthreads(8, 8, 1)] void CS_CoCTileFlatten(uint2 DTid : SV_DispatchThreadID) {
	if (any(DTid >= CoCTileDim))
		return;

	int2 base = int2(DTid) * COC_TILE_SIZE_FULLRES;
	float2 minMax = float2(1e4f, -1e4f);
	[loop] for (int y = 0; y < COC_TILE_SIZE_FULLRES; y += 2)
	{
		[loop] for (int x = 0; x < COC_TILE_SIZE_FULLRES; x += 2)
		{
			float2 uv = (float2(base + int2(x, y)) + 1.0f) * SharedData::BufferDim.zw;
			float4 g = TexCoCInput.GatherRed(LinearSampler, uv);
			minMax.x = min(minMax.x, min(min(g.x, g.y), min(g.z, g.w)));
			minMax.y = max(minMax.y, max(max(g.x, g.y), max(g.z, g.w)));
		}
	}
	// z is the foreground disc reach in full-resolution pixels. Propagating this value at tile
	// resolution replaces the old pair of 35-tap half-resolution mask filters.
	float nearReachPx = min(max(-minMax.x, 0.0f) * NearPlaneMaxBlur * cocToPixels * BokehMaxRadius, NearMaxReachPx);
	RWTexCoCTile[DTid] = float4(minMax, nearReachPx, 0.0f);
}

	// Separable min/max dilation plus a conservative Manhattan distance transform. A tile step is
	// scaled by 1/sqrt(2), so diagonal propagation never under-covers a circular aperture.
	[numthreads(8, 8, 1)] void CS_CoCTileDilateH(uint2 DTid : SV_DispatchThreadID)
{
	if (any(DTid >= CoCTileDim))
		return;

	int radius = (int)TileDilateRadius;
	int maxX = (int)CoCTileDim.x - 1;
	float2 minMax = float2(1e4f, -1e4f);
	float nearReachPx = 0.0f;
	[loop] for (int i = -radius; i <= radius; ++i)
	{
		float4 s = TexCoCTile[uint2(clamp((int)DTid.x + i, 0, maxX), DTid.y)];
		minMax.x = min(minMax.x, s.x);
		minMax.y = max(minMax.y, s.y);
		nearReachPx = max(nearReachPx, s.z - abs((float)i) * COC_TILE_SIZE_FULLRES * 0.70710678f);
	}
	RWTexCoCTile[DTid] = float4(minMax, max(nearReachPx, 0.0f), 0.0f);
}

[numthreads(8, 8, 1)] void CS_CoCTileDilateV(uint2 DTid : SV_DispatchThreadID) {
	if (any(DTid >= CoCTileDim))
		return;

	int radius = (int)TileDilateRadius;
	int maxY = (int)CoCTileDim.y - 1;
	float2 minMax = float2(1e4f, -1e4f);
	float nearReachPx = 0.0f;
	[loop] for (int i = -radius; i <= radius; ++i)
	{
		float4 s = TexCoCTile[uint2(DTid.x, clamp((int)DTid.y + i, 0, maxY))];
		minMax.x = min(minMax.x, s.x);
		minMax.y = max(minMax.y, s.y);
		nearReachPx = max(nearReachPx, s.z - abs((float)i) * COC_TILE_SIZE_FULLRES * 0.70710678f);
	}
	RWTexCoCTile[DTid] = float4(minMax, max(nearReachPx, 0.0f), 0.0f);
}

float SelectSetupCoC(float coc[4])
{
	// The nearest signed radius wins. In particular, a foreground/background edge keeps its
	// foreground identity instead of inventing a small radius by cancellation.
	return min(min(coc[0], coc[1]), min(coc[2], coc[3]));
}

float SelectReducedCoC(float coc[4])
{
	// Preserve the signed sample closest to focus. Coarse far-field taps then stop at a foreground
	// or sharp boundary instead of averaging across it, while uniform blur regions reduce normally.
	float selected = coc[0];
	[unroll] for (int i = 1; i < 4; ++i)
	{
		if (abs(selected) > abs(coc[i]))
			selected = coc[i];
	}
	return selected;
}

float GetOneSidedCoCWeight(float representativeCoC, float sampleCoC, float scale)
{
	// Background may leak into a foreground footprint for hole filling, but foreground is rejected
	// from a background footprint. This asymmetry is stable at signed-CoC layer boundaries.
	return saturate(1.0f - (representativeCoC - sampleCoC) * cocToPixels * scale);
}

float3 ReduceColorWithCoC(float3 tapColor[4], float tapCoC[4], float outCoC, float scale)
{
	float3 sum = 0.0f;
	float weightSum = 0.0f;
	[unroll] for (int i = 0; i < 4; ++i)
	{
		float weight = GetOneSidedCoCWeight(outCoC, tapCoC[i], scale);
		sum += tapColor[i] * weight;
		weightSum += weight;
	}
	return sum / max(weightSum, 1e-4f);
}

// Half res setup of scene colour and signed CoC.
[numthreads(8, 8, 1)] void CS_Downsample(uint2 DTid : SV_DispatchThreadID) {
	if (any(DTid >= HalfResDim))
		return;

	int2 base = int2(DTid) * 2;
	float3 tapColor[4];
	float tapCoC[4];
	[unroll] for (int i = 0; i < 4; ++i)
	{
		int2 p = ClampToBuffer(base + int2(i & 1, i >> 1));
		tapColor[i] = TexColor[p].rgb;
		tapCoC[i] = TexCoCInput[p].r;
	}

	float outCoC = SelectSetupCoC(tapCoC);
	float3 outColor = ReduceColorWithCoC(tapColor, tapCoC, outCoC, 0.25f);
	RWTexOut[DTid] = float4(AccentuateWhites(outColor), 1.0f);
	RWTexCoC[DTid] = outCoC;
}

	// Exact compatibility setup retained for A/B validation and user rollback.
	[numthreads(8, 8, 1)] void CS_DownsampleLegacy(uint2 DTid : SV_DispatchThreadID)
{
	if (any(DTid >= HalfResDim))
		return;

	int2 base = int2(DTid) * 2;
	float3 tapColor[4];
	float tapCoC[4];
	[unroll] for (int i = 0; i < 4; ++i)
	{
		int2 p = ClampToBuffer(base + int2(i & 1, i >> 1));
		tapColor[i] = TexColor[p].rgb;
		tapCoC[i] = TexCoCInput[p].r;
	}

	float outCoC = 0.25f * (tapCoC[0] + tapCoC[1] + tapCoC[2] + tapCoC[3]);
	float3 sum = 0.0f;
	float weightSum = 0.0f;
	[unroll] for (int j = 0; j < 4; ++j)
	{
		float weight = max(saturate(1.0f - abs(tapCoC[j] - outCoC) * cocToPixels * 0.25f), 1.0f / 16.0f);
		sum += tapColor[j] * weight;
		weightSum += weight;
	}

	RWTexOut[DTid] = float4(AccentuateWhites(sum / weightSum), 1.0f);
	RWTexCoC[DTid] = outCoC;
}

// Generic 2x2 pyramid reduction. The source sizes are queried from the bound resources so the
// same shader handles half -> quarter -> eighth -> sixteenth, including odd dimensions.
[numthreads(8, 8, 1)] void CS_ReduceColorCoC(uint2 DTid : SV_DispatchThreadID) {
	uint sourceWidth;
	uint sourceHeight;
	TexColor.GetDimensions(sourceWidth, sourceHeight);
	uint2 outputDim = max(uint2(1, 1), (uint2(sourceWidth, sourceHeight) + 1u) / 2u);
	if (any(DTid >= outputDim))
		return;

	int2 base = int2(DTid) * 2;
	int2 sourceMax = int2(sourceWidth, sourceHeight) - 1;
	float3 tapColor[4];
	float tapCoC[4];
	[unroll] for (int i = 0; i < 4; ++i)
	{
		int2 p = clamp(base + int2(i & 1, i >> 1), int2(0, 0), sourceMax);
		tapColor[i] = TexColor[p].rgb;
		tapCoC[i] = TexReduceCoCInput[p].r;
	}

	float outCoC = SelectReducedCoC(tapCoC);
	float mipScale = max((float)sourceWidth / max(SharedData::BufferDim.x * 0.5f, 1.0f), 0.125f);
	// 0.5*mipScale yields 0.25 / 0.125 / 0.0625 at the three reduce stages, keeping
	// the signed-CoC rejection threshold resolution independent.
	float3 outColor = ReduceColorWithCoC(tapColor, tapCoC, outCoC, 0.5f * mipScale);
	RWTexOut[DTid] = float4(outColor, 1.0f);
	RWTexCoC[DTid] = outCoC;
}

	// Color-only reduction for the near gather: the resolved far layer is its source, while footprint
	// selection reuses the signed setup CoC pyramid.
	[numthreads(8, 8, 1)] void CS_ReduceColor(uint2 DTid : SV_DispatchThreadID)
{
	uint sourceWidth;
	uint sourceHeight;
	TexColor.GetDimensions(sourceWidth, sourceHeight);
	uint2 outputDim = max(uint2(1, 1), (uint2(sourceWidth, sourceHeight) + 1u) / 2u);
	if (any(DTid >= outputDim))
		return;

	int2 base = int2(DTid) * 2;
	int2 sourceMax = int2(sourceWidth, sourceHeight) - 1;
	float3 color = 0.0f;
	[unroll] for (int i = 0; i < 4; ++i)
		color += TexColor[clamp(base + int2(i & 1, i >> 1), int2(0, 0), sourceMax)].rgb;
	RWTexOut[DTid] = float4(color * 0.25f, 1.0f);
}

#if defined(GATHER_RING_COUNT)
float GetGatherMip(float kernelRadiusInPixels)
{
	// Mip 0 is already half resolution. Match a tap's footprint to the average spacing of the
	// fixed kernel, then hold one mip for the entire output pixel to keep texture access coherent.
	float kernelRadiusInHalfResPixels = kernelRadiusInPixels * 0.5f;
	float sampleSpacing = kernelRadiusInHalfResPixels / (GATHER_RING_COUNT + 0.5f);
	return clamp(floor(0.5f + log2(max(sampleSpacing, 1.0f))), 0.0f, 3.0f);
}

float3 SampleGatherColor(float2 uv, float mip)
{
	float3 sampleColor = 0.0f;
	[branch] if (mip < 0.5f)
	{
		sampleColor = TexColor.SampleLevel(LinearSampler, uv, 0).rgb;
	}
	else if (mip < 1.5f)
	{
		sampleColor = TexGatherColor1.SampleLevel(LinearSampler, uv, 0).rgb;
	}
	else if (mip < 2.5f)
	{
		sampleColor = TexGatherColor2.SampleLevel(LinearSampler, uv, 0).rgb;
	}
	else
	{
		sampleColor = TexGatherColor3.SampleLevel(LinearSampler, uv, 0).rgb;
	}
	return sampleColor;
}

float SampleGatherCoC(float2 uv, float mip)
{
	float sampleCoC = 0.0f;
	[branch] if (mip < 0.5f)
	{
		sampleCoC = TexCoCHalf.SampleLevel(LinearSampler, uv, 0).r;
	}
	else if (mip < 1.5f)
	{
		sampleCoC = TexGatherCoC1.SampleLevel(LinearSampler, uv, 0).r;
	}
	else if (mip < 2.5f)
	{
		sampleCoC = TexGatherCoC2.SampleLevel(LinearSampler, uv, 0).r;
	}
	else
	{
		sampleCoC = TexGatherCoC3.SampleLevel(LinearSampler, uv, 0).r;
	}
	return sampleCoC;
}

struct AdaptiveBokehSample
{
	float2 offset;
	float radialWeight;
	float normalizedDistance;
	float coverage;
	float3 tint;
};

float2 RotateBokehOffset(float2 offset, float2 rotation)
{
	return float2(
		offset.x * rotation.x - offset.y * rotation.y,
		offset.x * rotation.y + offset.y * rotation.x);
}

AdaptiveBokehSample GetAdaptiveBokehSample(uint sampleIndex, float2 rotation)
{
	float4 data = BokehSamples[sampleIndex];
	AdaptiveBokehSample sample;
	sample.offset = data.xy;
	sample.radialWeight = data.z;
	sample.normalizedDistance = data.w;
	sample.coverage = 1.0f;
	sample.tint = 1.0f;

	[branch] if (BokehMode == 1)
	{
		float4 aperture = TexBokehShape.SampleLevel(LinearSampler, data.xy * 0.5f + 0.5f, 0);
		float luma = max(Color::RGBToLuminance(aperture.rgb), 0.0f);
		sample.coverage = sqrt(saturate(luma * aperture.a));
		sample.tint = min(aperture.rgb / max(luma, 1e-4f), 4.0f);
		sample.offset *= CustomShapeRadiusScale;
	}
	else
	{
		// The four-ring permutation uses the first 60 entries (r <= 0.8) and expands them to
		// the same unit aperture as the five-ring table. Custom tables already cover the full mask.
		sample.offset *= GATHER_SAMPLE_SCALE;
		sample.radialWeight *= GATHER_SAMPLE_SCALE;
		sample.normalizedDistance *= GATHER_SAMPLE_SCALE;
	}

	// Convolution is evaluated as gather, so the aperture sample is reflected around its centre.
	sample.offset = RotateBokehOffset(-sample.offset, rotation);
	return sample;
}

float CalculateGatherSampleWeight(float sampleRadiusInPixels, float ringDistanceInPixels, float mip)
{
	// Reduced input texels represent a wider footprint. Feather the CoC intersection over that
	// footprint, keeping 50% coverage where the sample disc exactly meets the canonical ring.
	float footprintInFullResPixels = exp2(mip + 1.0f);
	return saturate(
		(sampleRadiusInPixels - ringDistanceInPixels * NearFarDistanceCompensation) /
			footprintInFullResPixels +
		0.5f);
}

void GetAdaptiveBokehCenter(out float coverage, out float3 tint)
{
	coverage = 1.0f;
	tint = 1.0f;
	[branch] if (BokehMode == 1)
	{
		float4 aperture = TexBokehShape.SampleLevel(LinearSampler, (0.5f).xx, 0);
		float luma = max(Color::RGBToLuminance(aperture.rgb), 0.0f);
		coverage = sqrt(saturate(luma * aperture.a));
		tint = min(aperture.rgb / max(luma, 1e-4f), 4.0f);
	}
}

float LoadTileNearReachPixels(Texture2D<float4> tiles, uint2 Gid)
{
	return tiles[min(Gid, CoCTileDim - 1)].z;
}

float SampleNearReachPixels(float2 texcoord, float pixelCoC)
{
	float propagated = TexCoCTileDilated.SampleLevel(LinearSampler, texcoord, 0).z / max(BokehMaxRadius, 1.0f);
	float local = max(-pixelCoC, 0.0f) * NearPlaneMaxBlur * cocToPixels;
	return max(propagated, local);
}

void AccumulateFarGatherSample(
	uint sampleIndex,
	float2 rotation,
	float2 texcoord,
	float kernelRadiusInPixels,
	float mip,
	float colorRadius,
	float centerWeight,
	inout float3 colorSum,
	inout float weightSum)
{
	AdaptiveBokehSample bokeh = GetAdaptiveBokehSample(sampleIndex, rotation);
	float ringDistanceInPixels = bokeh.normalizedDistance * kernelRadiusInPixels;
	float2 unitOffset = ApplyPetzvalMorph(bokeh.offset, texcoord);
	float2 tapCoords = texcoord + unitOffset * kernelRadiusInPixels * SharedData::BufferDim.zw;
	float sampleRadius = SampleGatherCoC(tapCoords, mip);
	float ringWeight = lerp(bokeh.radialWeight, 1.0f, centerWeight);
	float weight = (sampleRadius >= 0.0f) * ringWeight *
	               CalculateGatherSampleWeight(sampleRadius * FarPlaneMaxBlur * cocToPixels, ringDistanceInPixels, mip) * bokeh.coverage;
	weight *= 1.0f + min(FarPlaneMaxBlur, 3.0f) * saturate((colorRadius - sampleRadius) * cocToPixels);
	[branch] if (weight > 0.0f)
	{
		colorSum += SampleGatherColor(tapCoords, mip) * bokeh.tint * weight;
		weightSum += weight;
	}
}

void AccumulateNearGatherSample(
	uint sampleIndex,
	float2 rotation,
	float2 texcoord,
	float kernelRadiusInPixels,
	float mip,
	float centerWeight,
	inout float3 colorSum,
	inout float weightSum)
{
	AdaptiveBokehSample bokeh = GetAdaptiveBokehSample(sampleIndex, rotation);
	float2 unitOffset = ApplyPetzvalMorph(bokeh.offset, texcoord);
	float2 tapCoords = texcoord + unitOffset * kernelRadiusInPixels * SharedData::BufferDim.zw;
	float weight = lerp(bokeh.radialWeight, 1.0f, smoothstep(0.0f, 1.0f, centerWeight)) * bokeh.coverage;
	colorSum += SampleGatherColor(tapCoords, mip) * bokeh.tint * weight;
	weightSum += weight;
}

[numthreads(8, 8, 1)] void CS_FarGather(uint2 DTid : SV_DispatchThreadID, uint2 Gid : SV_GroupID) {
	if (any(DTid >= HalfResDim))
		return;

	float4 color = TexColor[DTid];
	float2 tileCoC = LoadTileCoCMinMax(TexCoCTile, Gid);
	if (tileCoC.y < onePixelInCoC || FarPlaneMaxBlur <= 0.0f) {
		RWTexOut[DTid] = color;
		return;
	}

	float colorRadius = TexCoCHalf[DTid].r;
	if (colorRadius < onePixelInCoC) {
		RWTexOut[DTid] = color;
		return;
	}

	float2 texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	float kernelRadiusInPixels = colorRadius * FarPlaneMaxBlur * cocToPixels;
	float mip = GetGatherMip(kernelRadiusInPixels);
	float centerWeight = saturate(1.0f - BokehBusyFactor);
	float centerCoverage;
	float3 centerTint;
	GetAdaptiveBokehCenter(centerCoverage, centerTint);
	float3 colorSum = color.rgb * centerTint * centerWeight * centerCoverage;
	float weightSum = centerWeight * centerCoverage;
	float rotationAngle = -Math::TAU * HighlightShapeRotationAngle;
	float2 rotation;
	sincos(rotationAngle, rotation.y, rotation.x);

	[unroll] for (int i = 0; i < GATHER_SAMPLE_PAIR_COUNT; ++i)
	{
		uint2 pair = GatherSamplePairs[i];
		AccumulateFarGatherSample(pair.x, rotation, texcoord, kernelRadiusInPixels, mip, colorRadius, centerWeight, colorSum, weightSum);
		AccumulateFarGatherSample(pair.y, rotation, texcoord, kernelRadiusInPixels, mip, colorRadius, centerWeight, colorSum, weightSum);
	}

	color.rgb = colorSum / max(weightSum, 1e-4f);
	RWTexOut[DTid] = color;
}

	[numthreads(8, 8, 1)] void CS_NearGather(uint2 DTid : SV_DispatchThreadID, uint2 Gid : SV_GroupID)
{
	if (any(DTid >= HalfResDim))
		return;

	float4 color = TexColor[DTid];
	float tileNearReachPx = LoadTileNearReachPixels(TexCoCTileDilated, Gid);
	if (tileNearReachPx <= 1.0f || NearPlaneMaxBlur <= 0.0f) {
		color.a = 0.0f;
		RWTexOut[DTid] = color;
		return;
	}

	float pixelCoC = TexCoCHalf[DTid];
	float2 texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	float kernelRadiusInPixels = SampleNearReachPixels(texcoord, pixelCoC);
	if (kernelRadiusInPixels <= 1.0f) {
		color.a = 0.0f;
		RWTexOut[DTid] = color;
		return;
	}

	float mip = GetGatherMip(kernelRadiusInPixels);
	float centerWeight = saturate(1.0f - BokehBusyFactor);
	float centerCoverage;
	float3 centerTint;
	GetAdaptiveBokehCenter(centerCoverage, centerTint);
	float3 colorSum = color.rgb * centerTint * centerWeight * centerCoverage;
	float weightSum = centerWeight * centerCoverage;
	float rotationAngle = -Math::TAU * HighlightShapeRotationAngle;
	float2 rotation;
	sincos(rotationAngle, rotation.y, rotation.x);

	[unroll] for (int i = 0; i < GATHER_SAMPLE_PAIR_COUNT; ++i)
	{
		uint2 pair = GatherSamplePairs[i];
		AccumulateNearGatherSample(pair.x, rotation, texcoord, kernelRadiusInPixels, mip, centerWeight, colorSum, weightSum);
		AccumulateNearGatherSample(pair.y, rotation, texcoord, kernelRadiusInPixels, mip, centerWeight, colorSum, weightSum);
	}

	float blurredCoCInPixels = kernelRadiusInPixels / max(NearPlaneMaxBlur, 1e-4f);
	float pixelCoCInPixels = -pixelCoC * cocToPixels;
	float coverage = (blurredCoCInPixels > 1.0f) ? ((pixelCoC <= 0.0f) ? 2.0f : 1.0f) * blurredCoCInPixels :
	                                               max(blurredCoCInPixels, pixelCoCInPixels);
	color.rgb = colorSum / max(weightSum, 1e-4f);
	color.a = saturate((min(2.5f, NearPlaneMaxBlur) + 0.4f) * coverage * (1.0f / nearFullOpacityPixels));
	RWTexOut[DTid] = color;
}
#endif

[numthreads(8, 8, 1)] void CS_FarBlur(uint2 DTid : SV_DispatchThreadID, uint2 Gid : SV_GroupID) {
	float4 color = TexColor[DTid];

	// Group uniform early out: nothing in this tile has a far field blur disc worth gathering.
	float2 tileCoC = LoadTileCoCMinMax(TexCoCTile, Gid);
	if (tileCoC.y < onePixelInCoC || FarPlaneMaxBlur <= 0) {
		RWTexOut[DTid] = color;
		return;
	}

	float numberOfRings = round(BlurQuality);
	// Group uniform fast gather: a tile with a near constant CoC has no depth layers to resolve, so
	// one ring less is indistinguishable and saves ~1/4 of the taps at the default quality.
	if ((tileCoC.y - max(tileCoC.x, 0.0f)) < tileCoC.y * fastGatherCoCError)
		numberOfRings = max(numberOfRings - 1.0f, 2.0f);

	float2 texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	const float pointsFirstRing = 7;  // each ring has a multiple of this value of sample points.
	float colorRadius = TexCoCHalf[DTid].r;
	// we'll not process near plane fragments as they're processed in a separate pass.
	if (colorRadius < onePixelInCoC) {
		// near plane fragment, will be done in near plane pass
		RWTexOut[DTid] = color;
		return;
	}

	// gather kernel radius for this fragment, in full-res pixels
	float kernelRadiusInPixels = colorRadius * FarPlaneMaxBlur * cocToPixels;

	float bokehBusyFactorToUse = saturate(1.0 - BokehBusyFactor);  // use the busy factor as an edge bias on the blur, not the highlights
	float4 average = float4(color.rgb * bokehBusyFactorToUse, bokehBusyFactorToUse);
	float2 pointOffset = float2(0, 0);
	float2 ringRadiusDeltaCoords = (SharedData::BufferDim.zw * kernelRadiusInPixels) / numberOfRings;
	float2 currentRingRadiusCoords = ringRadiusDeltaCoords;
	float pixelsPerRing = kernelRadiusInPixels / numberOfRings;
	float ringDistanceInPixels = 0;
	float pointsOnRing = pointsFirstRing;
	bool useShape = HighlightShape > 0;
	float4 shapeTap = float4(1.0f, 1.0f, 1.0f, 1.0f);
	for (float ringIndex = 0; ringIndex < numberOfRings; ringIndex++) {
		float anglePerPoint = Math::TAU / pointsOnRing;
		float angle = anglePerPoint;
		float ringWeight = lerp(ringIndex / numberOfRings, 1, bokehBusyFactorToUse);
		ringDistanceInPixels += pixelsPerRing;
		float shapeRingDistance = ((ringIndex + 1) / numberOfRings) * 0.5f;
		for (float pointNumber = 0; pointNumber < pointsOnRing; pointNumber++) {
			sincos(angle, pointOffset.y, pointOffset.x);
			// shapeLuma is in Alpha
			if (useShape)
				shapeTap = GetShapeTap(angle, shapeRingDistance);
			pointOffset = ApplyPetzvalMorph(pointOffset, texcoord);
			float2 tapCoords = float2(texcoord + (pointOffset * currentRingRadiusCoords));
			float sampleRadius = TexCoCHalf.SampleLevel(LinearSampler, tapCoords, 0).r;
			float4 tap = 0;
			float weight = (sampleRadius >= 0) * ringWeight * CalculateSampleWeight(sampleRadius * FarPlaneMaxBlur * cocToPixels, ringDistanceInPixels) * (shapeTap.a > 0.01 ? 1.0f : 0.0f);
			// adjust the weight for samples which are in front of the fragment, as they have to get their weight boosted so we don't see edges bleeding through.
			// as otherwise they'll get a weight that's too low relatively to the pixels sampled from the plane the fragment is in.The 3.0 value is empirically determined.
			weight *= (1.0 + min(FarPlaneMaxBlur, 3.0f) * saturate((colorRadius - sampleRadius) * cocToPixels));
			if (weight > 0)
				tap = TexColor.SampleLevel(LinearSampler, tapCoords, 0);
			average.rgb += tap.rgb * weight;
			average.w += weight;
			angle += anglePerPoint;
		}
		pointsOnRing += pointsFirstRing;
		currentRingRadiusCoords += ringRadiusDeltaCoords;
	}
	color.rgb = average.rgb / (average.w + (average.w == 0));
	RWTexOut[DTid] = color;
}

	[numthreads(8, 8, 1)] void CS_NearBlur(uint2 DTid : SV_DispatchThreadID, uint2 Gid : SV_GroupID)
{
	float4 color = TexColor[DTid];

	// The tile buffer carries the remaining foreground reach in full-resolution pixels.
	float tileNearReachPx = TexCoCTileDilated[min(Gid, CoCTileDim - 1)].z;
	if (tileNearReachPx <= 1.0f || NearPlaneMaxBlur <= 0) {
		color.a = 0;
		RWTexOut[DTid] = color;
		return;
	}

	float2 texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	float pixelCoC = TexCoCHalf[DTid];
	float kernelRadiusInPixels = max(
		TexCoCTileDilated.SampleLevel(LinearSampler, texcoord, 0).z / max(BokehMaxRadius, 1.0f),
		max(-pixelCoC, 0.0f) * NearPlaneMaxBlur * cocToPixels);

	if (kernelRadiusInPixels <= 1.0f) {
		color.a = 0;
		RWTexOut[DTid] = color;
		return;
	}

	// use one extra ring as undersampling is really prominent in near-camera objects.
	float numberOfRings = max(round(BlurQuality), 1) + 1;
	float pointsFirstRing = 7;
	float bokehBusyFactorToUse = saturate(1.0 - BokehBusyFactor);  // use the busy factor as an edge bias on the blur, not the highlights
	float4 average = float4(color.rgb * bokehBusyFactorToUse, bokehBusyFactorToUse);
	float2 pointOffset = float2(0, 0);
	float2 ringRadiusDeltaCoords = SharedData::BufferDim.zw * (kernelRadiusInPixels / (numberOfRings - 1));
	float pointsOnRing = pointsFirstRing;
	float2 currentRingRadiusCoords = ringRadiusDeltaCoords;
	bool useShape = HighlightShape > 0;
	float4 shapeTap = float4(1.0f, 1.0f, 1.0f, 1.0f);
	for (float ringIndex = 0; ringIndex < numberOfRings; ringIndex++) {
		float anglePerPoint = Math::TAU / pointsOnRing;
		float angle = anglePerPoint;
		// no further weight needed, bleed all you want.
		float weight = lerp(ringIndex / numberOfRings, 1, smoothstep(0, 1, bokehBusyFactorToUse));
		float shapeRingDistance = ((ringIndex + 1) / numberOfRings) * 0.5f;
		for (float pointNumber = 0; pointNumber < pointsOnRing; pointNumber++) {
			sincos(angle, pointOffset.y, pointOffset.x);
			// shapeLuma is in Alpha
			if (useShape)
				shapeTap = GetShapeTap(angle, shapeRingDistance);
			pointOffset = ApplyPetzvalMorph(pointOffset, texcoord);
			float2 tapCoords = float2(texcoord + (pointOffset * currentRingRadiusCoords));
			float sampleWeight = weight * (shapeTap.a > 0.01 ? 1.0f : 0.0f);
			if (sampleWeight > 0) {
				float4 tap = TexColor.SampleLevel(LinearSampler, tapCoords, 0);
				average.rgb += tap.rgb * sampleWeight;
				average.w += sampleWeight;
			}
			angle += anglePerPoint;
		}
		pointsOnRing += pointsFirstRing;
		currentRingRadiusCoords += ringRadiusDeltaCoords;
	}
	average.rgb /= (average.w + (average.w == 0));

	// Opacity of the near field layer. Expressed in pixels so it is independent of the CoC scale.
	float blurredCoCInPixels = kernelRadiusInPixels / max(NearPlaneMaxBlur, 1e-4f);
	float pixelCoCInPixels = -pixelCoC * cocToPixels;  // > 0 when this fragment is itself in the near field
	float coverage = (blurredCoCInPixels > 1.0f) ? ((pixelCoC <= 0) ? 2.0f : 1.0f) * blurredCoCInPixels :
	                                               max(blurredCoCInPixels, pixelCoCInPixels);
	float alpha = saturate((min(2.5, NearPlaneMaxBlur) + 0.4) * coverage * (1.0f / nearFullOpacityPixels));

	color.rgb = average.rgb;
	color.a = alpha;
	RWTexOut[DTid] = color;
}

float4 Median3(float4 a, float4 b, float4 c)
{
	return max(min(a, b), min(max(a, b), c));
}

float4 Median9(float4 samples[9])
{
	float4 low[3];
	float4 middle[3];
	float4 high[3];
	[unroll] for (int row = 0; row < 3; ++row)
	{
		float4 a = samples[row * 3];
		float4 b = samples[row * 3 + 1];
		float4 c = samples[row * 3 + 2];
		low[row] = min(a, min(b, c));
		high[row] = max(a, max(b, c));
		middle[row] = Median3(a, b, c);
	}
	return Median3(
		max(low[0], max(low[1], low[2])),
		Median3(middle[0], middle[1], middle[2]),
		min(high[0], min(high[1], high[2])));
}

float4 GatherMedianAt(Texture2D<float4> inputTexture, uint2 pixel)
{
	float4 samples[9];
	[unroll] for (int i = 0; i < 9; ++i)
	{
		int2 offset = int2(i % 3, i / 3) - 1;
		samples[i] = inputTexture[ClampToHalfRes(int2(pixel) + offset)];
	}
	return Median9(samples);
}

[numthreads(8, 8, 1)] void CS_GatherPostfilter(uint2 DTid : SV_DispatchThreadID) {
	if (any(DTid >= HalfResDim))
		return;

	// u3 is the otherwise idle tile UAV slot. Filtering both layers in one dispatch avoids adding a
	// second postfilter dispatch while giving near and far identical outlier rejection.
	RWTexOut[DTid] = GatherMedianAt(TexColor, DTid);
	RWTexCoCTile[DTid] = GatherMedianAt(TexNearBlur, DTid);
}

	[numthreads(8, 8, 1)] void CS_Combiner(uint2 DTid : SV_DispatchThreadID)
{
	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	// first blend far plane with original buffer, then near plane on top of that.
	float4 originalFragment = TexColor[DTid];
	originalFragment.rgb = AccentuateWhites(originalFragment.rgb);
	float4 farFragment = TexFarBlur.SampleLevel(LinearSampler, uv, 0);
	float4 nearFragment = TexNearBlur.SampleLevel(LinearSampler, uv, 0);
	float pixelCoC = TexCoCInput[DTid].r;
	// multiply with far plane max blur so if we need to have 0 blur we get full res
	float realCoC = pixelCoC * saturate(FarPlaneMaxBlur);
	// Fully use the (half res) far field once the blur disc is bigger than a couple of pixels, and
	// blend below that so leaving the focal plane doesn't pop in resolution.
	float blendFactor = smoothstep(0.0f, 1.0f, saturate(realCoC / (2.0f * onePixelInCoC)));
	float4 color;
	color = lerp(originalFragment, farFragment, blendFactor);
	color.rgb = lerp(color.rgb, nearFragment.rgb, nearFragment.a * (NearPlaneMaxBlur != 0));
	color.a = 1.0;
	RWTexOut[DTid] = color;
}

[numthreads(8, 8, 1)] void CS_PostSmoothing1(uint2 DTid : SV_DispatchThreadID) {
	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	RWTexOut[DTid] = PerformFullFragmentGaussianBlur(TexColor, uv, DTid, float2((SharedData::BufferDim.z), 0.0));
}

	[numthreads(8, 8, 1)] void CS_PostSmoothing2AndFocusing(uint2 DTid : SV_DispatchThreadID)
{
	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	float4 color = PerformFullFragmentGaussianBlur(TexPostSmoothInput, uv, DTid, float2(0.0, (SharedData::BufferDim.w)));
	float4 originalColor = TexColor[DTid];

	// Ramp the smoothed result back in over the first few pixels of blur so in-focus geometry is untouched.
	float cocInPixels = abs(TexCoCInput[DTid].r) * cocToPixels;
	color.rgb = lerp(originalColor.rgb, color.rgb, saturate(cocInPixels < 1.0f ? 0.0f : cocInPixels * 0.25f));

	RWTexOut[DTid] = float4(color.rgb, 1.0f);
}
