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

RWTexture2D<float4> RWTexOut : register(u0);
RWTexture2D<float> RWFocus : register(u1);
RWTexture2D<float> RWTexCoC : register(u2);

SamplerState LinearSampler : register(s0);

Texture2D<float4> TexColor : register(t0);
Texture2D<float> TexPreviousFocus : register(t1);
Texture2D<float> DepthTexture : register(t2);
Texture2D<float> TexCoCInput : register(t3);
Texture2D<float> TexCoCBlurredInput : register(t4);
Texture2D<float4> TexFarBlur : register(t5);
Texture2D<float4> TexNearBlur : register(t6);
Texture2D<float4> TexPostSmoothInput : register(t7);
Texture2D<float4> TexBokehShape : register(t8);

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
	uint AutoFocus;
};

#define SENSOR_SIZE 0.024f

struct FocusInfo
{
	float2 texcoord;
	float focusDepth;
	float focusDepthInM;
	float focusDepthInMM;
	float pixelSizeLength;
	float nearPlaneInMM;
	float farPlaneInMM;
};

struct DiscBlurInfo
{
	float2 texcoord;
	float numberOfRings;
	float farPlaneMaxBlurInPixels;
	float nearPlaneMaxBlurInPixels;
	float cocFactorPerPixel;
	float highlightBoostFactor;
};

float GetDepth(float2 uv)
{
	float depth = DepthTexture.SampleLevel(LinearSampler, uv, 0);
	depth = SharedData::GetScreenDepth(depth) * GAME_UNIT_TO_M * 0.001f; // in KM
	return max(depth, 1e-6);
}

float PreviousFocus()
{
	return TexPreviousFocus[uint2(0, 0)].x;
}

void FillFocusInfoData(inout FocusInfo toFill)
{
	// Reshade depth buffer ranges from 0.0->1.0, where 1.0 is 1000 in world units. All camera element sizes are in mm, so we state 1 in world units is
	// 1 meter. This means to calculate from the linearized depth buffer value to meter we have to multiply by 1000.
	// Manual focus value is already in meter (well, sort of. This differs per game so we silently assume it's meter), so we first divide it by
	// 1000 to make it equal to a depth value read from the depth linearized depth buffer.
	// Read from sampler on current focus which is a 1x1 texture filled with the actual depth value of the focus point to use.
	toFill.focusDepth = PreviousFocus();
	toFill.focusDepthInM = toFill.focusDepth * 1000.0;       // km to m
	toFill.focusDepthInMM = toFill.focusDepthInM * 1000.0;   // m to mm
	toFill.pixelSizeLength = length(SharedData::BufferDim.xy);  // in pixels

	// HyperFocal calculation, see https://photo.stackexchange.com/a/33898. Useful to calculate the edges of the depth of field area
	float hyperFocal = (FocalLength * FocalLength) / (FNumber * SENSOR_SIZE);
	float hyperFocalFocusDepthFocus = (hyperFocal * toFill.focusDepthInMM);
	toFill.nearPlaneInMM = (hyperFocalFocusDepthFocus / (hyperFocal + (toFill.focusDepthInMM - FocalLength)));  // in mm
	toFill.farPlaneInMM = hyperFocalFocusDepthFocus / (hyperFocal - (toFill.focusDepthInMM - FocalLength));     // in mm
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
	float2 shapeTapCoords = float2((shapeRingDistance * pointOffsetForShape) + 0.5f);	// shapeRingDistance is [0, 0.5] so no need to multiply with 0.5 again
	float4 shapeTap = TexBokehShape.SampleLevel(LinearSampler, shapeTapCoords, 0);
	shapeTap.a = Color::RGBToLuminance(shapeTap.rgb);
	return shapeTap;
}

float CalculateBlurDiscSize(FocusInfo focusInfo)
{
	float pixelDepth = GetDepth(focusInfo.texcoord);
	float pixelDepthInM = pixelDepth * 1000.0;  // in meter

	// CoC (blur disc size) calculation based on [Lee2008]
	// CoC = ((EF / Zf - F) * (abs(Z-Zf) / Z)
	// where E is aperture size in mm, F is focal length in mm, Zf is depth of focal plane in mm, Z is depth of pixel in mm.
	// To calculate aperture in mm, we use D = F/N, where F is focal length and N is f-number
	// For the people getting confused:
	// Remember element sizes are in mm, our depth sizes are in meter, so we have to divide S1 by 1000 to get from meter -> mm. We don't have to
	// divide the elements in the 'abs(x-S1)/x' part, as the 1000.0 will then simply be muted out (as  a / (x/1000) == a * (1000/x))
	// formula: (((f*f) / N) / ((S1/1000.0) -f)) * (abs(x - S1) / x)
	// where f = FocalLength, N = FNumber, S1 = focusInfo.focusDepthInM, x = pixelDepthInM. In-lined to save on registers.
	float cocInMM = (((FocalLength * FocalLength) / FNumber) / ((focusInfo.focusDepthInM / 1000.0) - FocalLength)) *
	                (abs(pixelDepthInM - focusInfo.focusDepthInM) / (pixelDepthInM + (pixelDepthInM == 0)));
	float toReturn = max(abs(cocInMM) * SENSOR_SIZE, 0);  // divide by sensor size to get coc in % of screen (or better: in sampler units)
	return (pixelDepth < focusInfo.focusDepth) ? -toReturn : toReturn;
}

float GetBlurDiscRadiusFromSource(Texture2D<float> source, float2 texcoord, bool flattenToZero)
{
	float coc = source.SampleLevel(LinearSampler, texcoord, 0).x;
	// we're only interested in negative coc's (near plane). All coc's in focus/far plane are flattened to 0. Return the
	// absolute value of the coc as we're working with positive blurred CoCs (as the sign is no longer needed)
	return (flattenToZero && coc >= 0) ? 0 : abs(coc);
}

float PerformSingleValueGaussianBlur(Texture2D<float> source, float2 texcoord, float2 offsetWeight, bool flattenToZero)
{
	const float offset[18] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4661974725, 21.4627427973, 23.4592916956, 25.455844494, 27.4524015179, 29.4489630909, 31.445529535, 33.4421011704 };
	const float weight[18] = { 0.033245, 0.0659162217, 0.0636705814, 0.0598194658, 0.0546642566, 0.0485871646, 0.0420045997, 0.0353207015, 0.0288880982, 0.0229808311, 0.0177815511, 0.013382297, 0.0097960001, 0.0069746748, 0.0048301008, 0.0032534598, 0.0021315311, 0.0013582974 };

	float coc = GetBlurDiscRadiusFromSource(source, texcoord, flattenToZero);
	coc *= weight[0];

	float2 factorToUse = offsetWeight * NearPlaneMaxBlur * 0.8f;
	for (int i = 1; i < 18; ++i) {
		float2 coordOffset = factorToUse * offset[i];
		float weightSample = weight[i];
		coc += GetBlurDiscRadiusFromSource(source, texcoord + coordOffset, flattenToZero) * weightSample;
		coc += GetBlurDiscRadiusFromSource(source, texcoord - coordOffset, flattenToZero) * weightSample;
	}

	return saturate(coc);
}

float3 ConeOverlap(float3 fragment)
{
	float k = 0.4 * 0.33;
	float2 f = float2(1 - 2 * k, k);
	float3x3 m = float3x3(f.xyy, f.yxy, f.yyx);
	return mul(fragment, m);
}

float3 AccentuateWhites(float3 fragment)
{
	// apply small tow to the incoming fragment, so the whitepoint gets slightly lower than max.
	// We don't need to de-tonemap since we are under HDR.
	// fragment = pow(abs(ConeOverlap(fragment)), 1);
	return fragment / (HighlightBoost > 0.f ? max((1.001 - (HighlightBoost * fragment)), 0.001) : 1.0f);
}

// returns 2 vectors, (x,y) are up vector, (z,w) are right vector.
// In: pixelVector which is the current pixel converted into a vector where (0,0) is the center of the screen.
float4 CalculateAnamorphicFactor(float2 pixelVector)
{
	float HighlightAnamorphicFactor = 1.0f;
	float HighlightAnamorphicSpreadFactor = 0.0f;
	float normalizedFactor = lerp(1, HighlightAnamorphicFactor, lerp(length(pixelVector * 2), 1, HighlightAnamorphicSpreadFactor));
	return float4(0, 1 + (1 - normalizedFactor), normalizedFactor, 0);
}

// Calculates a rotation matrix for the current pixel specified in texcoord, which can be used to rotate the bokeh shape to match
// a distored field around the center of the screen: it rotates the anamorphic factors with this matrix so the bokeh shapes form a circle
// around the center of the screen.
float2x2 CalculateAnamorphicRotationMatrix(float2 texcoord)
{
	float HighlightAnamorphicAlignmentFactor = 0.0f;
	float2 pixelVector = normalize(texcoord - 0.5);
	float limiter = (1 - HighlightAnamorphicAlignmentFactor) / 2;
	pixelVector.y = clamp(pixelVector.y, -limiter, limiter);
	float2 refVector = normalize(float2(-0.5, 0));
	float2 sincosFactor = float2(0, 0);
	// calculate the angle between the pixelvector and the ref vector and grab the sin/cos for that angle for the rotation matrix.
	sincos(atan2(pixelVector.y, pixelVector.x) - atan2(refVector.y, refVector.x), sincosFactor.x, sincosFactor.y);
	return float2x2(sincosFactor.y, sincosFactor.x, -sincosFactor.x, sincosFactor.y);
}

// calculate the sample weight based on the values specified.
float CalculateSampleWeight(float sampleRadiusInCoC, float ringDistanceInCoC)
{
	return saturate(sampleRadiusInCoC - (ringDistanceInCoC * NearFarDistanceCompensation) + 0.5);
}

float2 MorphPointOffsetWithAnamorphicDeltas(float2 pointOffset, float4 anamorphicFactors, float2x2 anamorphicRotationMatrix)
{
	pointOffset.x = pointOffset.x * anamorphicFactors.x + pointOffset.x * anamorphicFactors.z;
	pointOffset.y = pointOffset.y * anamorphicFactors.y + pointOffset.y * anamorphicFactors.w;
	return mul(pointOffset, anamorphicRotationMatrix);
}

// Gathers min CoC from a horizontal range of pixels around the pixel at texcoord, for a range of -TILE_SIZE+1 to +TILE_SIZE+1.
// returns minCoC
float PerformTileGatherHorizontal(uint2 DTid)
{
	float tileSize = 1;
	float minCoC = 10;
	float coc;
	float2 offset = uint2(1, 0);
	for (float i = 0; i <= tileSize; ++i) {
		coc = TexCoCInput[DTid + offset].r;
		minCoC = min(minCoC, coc);
		coc = TexCoCInput[DTid - offset].r;
		minCoC = min(minCoC, coc);
		offset.x += 1;
	}
	return minCoC;
}

// Gathers min CoC from a vertical range of pixels around the pixel at texcoord from the high-res focus plane, for a range of -TILE_SIZE+1 to +TILE_SIZE+1.
// returns min CoC
float PerformTileGatherVertical(uint2 DTid)
{
	float tileSize = 1;
	float minCoC = 10;
	float coc;
	float2 offset = uint2(0, 1);
	for (float i = 0; i <= tileSize; ++i) {
		coc = TexCoCInput[DTid + offset].r;
		minCoC = min(minCoC, coc);
		coc = TexCoCInput[DTid - offset].r;
		minCoC = min(minCoC, coc);
		offset.y += 1;
	}
	return minCoC;
}

// Gathers the min CoC of the tile at texcoord and the 8 tiles around it.
float PerformNeighborTileGather(uint2 DTid)
{
	float minCoC = 10;
	float tileSizeX = 1;
	float tileSizeY = 1;
	// tile is TILE_SIZE*2+1 wide. So add that and substract that to get to neighbor tile right/left.
	// 3x3 around center.
	uint2 baseOffset = uint2(tileSizeX * 2 + 1, tileSizeY * 2 + 1);
	for (float i = -1; i < 2; i++) {
		for (float j = -1; j < 2; j++) {
			uint2 coordOffset = uint2(baseOffset.x * i, baseOffset.y * j);
			float coc = TexCoCInput[DTid + coordOffset].r;
			minCoC = min(minCoC, coc);
		}
	}
	return minCoC;
}

// Performs a small blur to the out of focus areas using a lower amount of rings. Additionally it calculates the luma of the fragment into alpha
// and makes sure the fragment post-blur has the maximum luminosity from the taken samples to preserve harder edges on highlights.
// In:	blurInfo, the pre-calculated disc blur information from the vertex shader.
// 		source, the source buffer to read RGBA data from
// Out: RGBA fragment that's the result of the disc-blur on the pixel at texcoord in source. A contains luma of RGB.
float4 PerformPreDiscBlur(DiscBlurInfo blurInfo, Texture2D source)
{
	const float radiusFactor = 1.0 / max(blurInfo.numberOfRings, 1);
	const float pointsFirstRing = max(blurInfo.numberOfRings - 3, 2);  // each ring has a multiple of this value of sample points.

	float4 fragment = source.SampleLevel(LinearSampler, blurInfo.texcoord, 0);
	fragment.rgb = AccentuateWhites(fragment.rgb);
	return fragment;
}

// Calculates the new RGBA fragment for a pixel at texcoord in source using a disc based blur technique described in [Jimenez2014]
// (Though without using tiles). Blurs far plane.
// In:	blurInfo, the pre-calculated disc blur information from the vertex shader.
// 		source, the source buffer to read RGBA data from. RGB is in HDR. A not used.
//		shape, the shape sampler to use if shapes are used.
// Out: RGBA fragment that's the result of the disc-blur on the pixel at texcoord in source. A contains luma of pixel.
float4 PerformDiscBlur(DiscBlurInfo blurInfo, Texture2D source)
{
	const float pointsFirstRing = 7;  // each ring has a multiple of this value of sample points.
	float4 fragment = source.SampleLevel(LinearSampler, blurInfo.texcoord, 0);
	float fragmentRadius = TexCoCInput.SampleLevel(LinearSampler, blurInfo.texcoord, 0).r;
	// we'll not process near plane fragments as they're processed in a separate pass.
	if (fragmentRadius < 0 || blurInfo.farPlaneMaxBlurInPixels <= 0) {
		// near plane fragment, will be done in near plane pass
		return fragment;
	}
	float bokehBusyFactorToUse = saturate(1.0 - BokehBusyFactor);  // use the busy factor as an edge bias on the blur, not the highlights
	float4 average = float4(fragment.rgb * fragmentRadius * bokehBusyFactorToUse, bokehBusyFactorToUse);
	float2 pointOffset = float2(0, 0);
	float2 ringRadiusDeltaCoords = (SharedData::BufferDim.zw * blurInfo.farPlaneMaxBlurInPixels * fragmentRadius) / blurInfo.numberOfRings;
	float2 currentRingRadiusCoords = ringRadiusDeltaCoords;
	float cocPerRing = (fragmentRadius * FarPlaneMaxBlur) / blurInfo.numberOfRings;
	float pointsOnRing = pointsFirstRing;
	float4 anamorphicFactors = CalculateAnamorphicFactor(blurInfo.texcoord - 0.5);  // xy are up vector, zw are right vector
	float2x2 anamorphicRotationMatrix = CalculateAnamorphicRotationMatrix(blurInfo.texcoord);
	bool useShape = HighlightShape > 0;
	float4 shapeTap = float4(1.0f, 1.0f, 1.0f, 1.0f);
	for (float ringIndex = 0; ringIndex < blurInfo.numberOfRings; ringIndex++) {
		float anglePerPoint = Math::TAU / pointsOnRing;
		float angle = anglePerPoint;
		float ringWeight = lerp(ringIndex / blurInfo.numberOfRings, 1, bokehBusyFactorToUse);
		float ringDistance = cocPerRing * ringIndex;
		float shapeRingDistance = ((ringIndex + 1) / blurInfo.numberOfRings) * 0.5f;
		for (float pointNumber = 0; pointNumber < pointsOnRing; pointNumber++) {
			sincos(angle, pointOffset.y, pointOffset.x);
			// shapeLuma is in Alpha
			shapeTap = useShape ? GetShapeTap(angle, shapeRingDistance) : shapeTap;
			// now transform the offset vector with the anamorphic factors and rotate it accordingly to the rotation matrix, so we get a nice
			// bending around the center of the screen.
			pointOffset = useShape ? pointOffset : MorphPointOffsetWithAnamorphicDeltas(pointOffset, anamorphicFactors, anamorphicRotationMatrix);
			float2 tapCoords = float2(blurInfo.texcoord + (pointOffset * currentRingRadiusCoords));
			float sampleRadius = TexCoCInput.SampleLevel(LinearSampler, tapCoords, 0).r;
			float weight = (sampleRadius >= 0) * ringWeight * CalculateSampleWeight(sampleRadius * FarPlaneMaxBlur, ringDistance) * (shapeTap.a > 0.01 ? 1.0f : 0.0f);
			// adjust the weight for samples which are in front of the fragment, as they have to get their weight boosted so we don't see edges bleeding through.
			// as otherwise they'll get a weight that's too low relatively to the pixels sampled from the plane the fragment is in.The 3.0 value is empirically determined.
			weight *= (1.0 + min(FarPlaneMaxBlur, 3.0f) * saturate(fragmentRadius - sampleRadius));
			float4 tap = source.SampleLevel(LinearSampler, tapCoords, 0);
			average.rgb += tap.rgb * weight;
			average.w += weight;
			angle += anglePerPoint;
		}
		pointsOnRing += pointsFirstRing;
		currentRingRadiusCoords += ringRadiusDeltaCoords;
	}
	fragment.rgb = average.rgb / (average.w + (average.w == 0));
	return fragment;
}

float4 PerformNearPlaneDiscBlur(DiscBlurInfo blurInfo, Texture2D source)
{
	float4 fragment = source.SampleLevel(LinearSampler, blurInfo.texcoord, 0);
	// r contains blurred CoC, g contains original CoC. Original is negative.
	float2 fragmentRadii = float2(TexCoCBlurredInput.SampleLevel(LinearSampler, blurInfo.texcoord, 0), TexCoCInput.SampleLevel(LinearSampler, blurInfo.texcoord, 0));
	float fragmentRadiusToUse = fragmentRadii.r;

	if (fragmentRadii.r <= 0) {
		// the blurred CoC value is still 0, we'll never end up with a pixel that has a different value than fragment, so abort now by
		// returning the fragment we already read.
		fragment.a = 0;
		return fragment;
	}

	// use one extra ring as undersampling is really prominent in near-camera objects.
	float numberOfRings = max(blurInfo.numberOfRings, 1) + 1;
	float pointsFirstRing = 7;
	// luma is stored in alpha
	float bokehBusyFactorToUse = saturate(1.0 - BokehBusyFactor);  // use the busy factor as an edge bias on the blur, not the highlights
	float4 average = float4(fragment.rgb * fragmentRadiusToUse * bokehBusyFactorToUse, bokehBusyFactorToUse);
	float2 pointOffset = float2(0, 0);
	float nearPlaneBlurInPixels = blurInfo.nearPlaneMaxBlurInPixels * fragmentRadiusToUse;
	float2 ringRadiusDeltaCoords = float2(SharedData::BufferDim.z, SharedData::BufferDim.w) * (nearPlaneBlurInPixels / (numberOfRings - 1));
	float pointsOnRing = pointsFirstRing;
	float2 currentRingRadiusCoords = ringRadiusDeltaCoords;
	float4 anamorphicFactors = CalculateAnamorphicFactor(blurInfo.texcoord - 0.5);  // xy are up vector, zw are right vector
	float2x2 anamorphicRotationMatrix = CalculateAnamorphicRotationMatrix(blurInfo.texcoord);
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
			shapeTap = useShape ? GetShapeTap(angle, shapeRingDistance) : shapeTap;
			// now transform the offset vector with the anamorphic factors and rotate it accordingly to the rotation matrix, so we get a nice
			// bending around the center of the screen.
			pointOffset = useShape ? pointOffset : MorphPointOffsetWithAnamorphicDeltas(pointOffset, anamorphicFactors, anamorphicRotationMatrix);
			float2 tapCoords = float2(blurInfo.texcoord + (pointOffset * currentRingRadiusCoords));
			float4 tap = source.SampleLevel(LinearSampler, tapCoords, 0);
			// r contains blurred CoC, g contains original CoC. Original can be negative
			float2 sampleRadii = float2(TexCoCBlurredInput.SampleLevel(LinearSampler, tapCoords, 0), TexCoCInput.SampleLevel(LinearSampler, tapCoords, 0));
			float blurredSampleRadius = sampleRadii.r;
			float sampleWeight = weight * (shapeTap.a > 0.01 ? 1.0f : 0.0f);
			average.rgb += tap.rgb * sampleWeight;
			average.w += sampleWeight;
			angle += anglePerPoint;
		}
		pointsOnRing += pointsFirstRing;
		currentRingRadiusCoords += ringRadiusDeltaCoords;
	}
	average.rgb /= (average.w + (average.w == 0));
	float alpha = saturate((min(2.5, NearPlaneMaxBlur) + 0.4) * (fragmentRadiusToUse > 0.1 ? (fragmentRadii.g <= 0 ? 2 : 1) * fragmentRadiusToUse : max(fragmentRadiusToUse, -fragmentRadii.g)));
	fragment.rgb = average.rgb;
	fragment.a = alpha;
	return fragment;
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
	float lengthPixelSize = length(float2(SharedData::BufferDim.z, SharedData::BufferDim.w));

	if (absoluteCoC < 0.2 || PostBlurSmoothing < 0.01 || fragmentLuma < 0.3) {
		// in focus or postblur smoothing isn't enabled or not really a highlight, ignore
		return fragment;
	}

	fragment *= weight[0];
	float2 factorToUse = offsetWeight * PostBlurSmoothing;

	for (int i = 1; i < 6; ++i) {
		float2 coordOffset = factorToUse * offset[i];
		float weightSample = weight[i];
		float sampleCoC = TexCoCInput.SampleLevel(LinearSampler, texcoord + coordOffset, 0).r;
		float maskFactor = abs(sampleCoC) < 0.2;

		fragment += (originalFragment * maskFactor * weightSample) +
		            (source.SampleLevel(LinearSampler, texcoord + coordOffset, 0) * (1 - maskFactor) * weightSample);

		sampleCoC = TexCoCInput.SampleLevel(LinearSampler, texcoord - coordOffset, 0).r;
		maskFactor = abs(sampleCoC) < 0.2;

		fragment += (originalFragment * maskFactor * weightSample) +
		            (source.SampleLevel(LinearSampler, texcoord - coordOffset, 0) * (1 - maskFactor) * weightSample);
	}
	return fragment;
}

[numthreads(1, 1, 1)] void CS_UpdateFocus(uint2 DTid
										  : SV_DispatchThreadID) {
	float depth = AutoFocus ? GetDepth(FocusCoord) : ManualFocusPlane;
	float previousFocus = TexPreviousFocus[uint2(0, 0)];
	RWFocus[DTid] = lerp(previousFocus, depth, TransitionSpeed);
}

[numthreads(8, 8, 1)] void CS_CalculateCoC(uint2 DTid
											: SV_DispatchThreadID)
{
	if (DTid.x >= (uint)SharedData::BufferDim.x || DTid.y >= (uint)SharedData::BufferDim.y)
		return;

	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	float4 color = TexColor[DTid];

	FocusInfo focusInfo;
	focusInfo.texcoord = uv;
	FillFocusInfoData(focusInfo);

	float coc = CalculateBlurDiscSize(focusInfo);
	RWTexCoC[DTid] = coc;
}

[numthreads(8, 8, 1)] void CS_CoCTile1(uint2 DTid
									   : SV_DispatchThreadID) {
	RWTexCoC[DTid] = PerformTileGatherHorizontal(DTid);
}

[numthreads(8, 8, 1)] void CS_CoCTile2(uint2 DTid
										: SV_DispatchThreadID) {
	RWTexCoC[DTid] = PerformTileGatherVertical(DTid);
}

[numthreads(8, 8, 1)] void CS_CoCTileNeighbor(uint2 DTid
											  : SV_DispatchThreadID) {
	RWTexCoC[DTid] = PerformNeighborTileGather(DTid);
}

[numthreads(8, 8, 1)] void CS_CoCGaussian1(uint2 DTid
											: SV_DispatchThreadID)
{
	float2 uv = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	RWTexCoC[DTid] = PerformSingleValueGaussianBlur(TexCoCInput, uv, float2(2.0f * SharedData::BufferDim.z, 0.0f), true);
}

[numthreads(8, 8, 1)] void CS_CoCGaussian2(uint2 DTid
										   : SV_DispatchThreadID) {
	float2 uv = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	RWTexCoC[DTid] = PerformSingleValueGaussianBlur(TexCoCInput, uv, float2(0.0f, 2.0f * SharedData::BufferDim.w), false);
}

[numthreads(8, 8, 1)] void CS_Blur(uint2 DTid
									: SV_DispatchThreadID)
{
	DiscBlurInfo blurInfo;
	blurInfo.texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	blurInfo.numberOfRings = round(BlurQuality);
	float pixelSizeLength = length(SharedData::BufferDim.zw) * 0.5f;
	blurInfo.farPlaneMaxBlurInPixels = (FarPlaneMaxBlur / 100.0f) / pixelSizeLength;
	blurInfo.nearPlaneMaxBlurInPixels = (NearPlaneMaxBlur / 100.0f) / pixelSizeLength;
	blurInfo.cocFactorPerPixel = pixelSizeLength * blurInfo.farPlaneMaxBlurInPixels;  // not needed for near plane.
	// Pre Blur
	float4 color = PerformPreDiscBlur(blurInfo, TexColor);
	RWTexOut[DTid] = color;
}

[numthreads(8, 8, 1)] void CS_FarBlur(uint2 DTid
									  : SV_DispatchThreadID) {
	DiscBlurInfo blurInfo;
	blurInfo.texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	blurInfo.numberOfRings = round(BlurQuality);
	float pixelSizeLength = length(SharedData::BufferDim.zw) * 0.5f;
	blurInfo.farPlaneMaxBlurInPixels = (FarPlaneMaxBlur / 100.0f) / pixelSizeLength;
	blurInfo.nearPlaneMaxBlurInPixels = (NearPlaneMaxBlur / 100.0f) / pixelSizeLength;
	blurInfo.cocFactorPerPixel = pixelSizeLength * blurInfo.farPlaneMaxBlurInPixels;  // not needed for near plane.
	float4 color = PerformDiscBlur(blurInfo, TexColor);
	RWTexOut[DTid] = color;
}

[numthreads(8, 8, 1)] void CS_NearBlur(uint2 DTid
										: SV_DispatchThreadID)
{
	DiscBlurInfo blurInfo;
	blurInfo.texcoord = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	blurInfo.numberOfRings = round(BlurQuality);
	float pixelSizeLength = length(SharedData::BufferDim.zw) * 0.5f;
	blurInfo.farPlaneMaxBlurInPixels = (FarPlaneMaxBlur / 100.0f) / pixelSizeLength;
	blurInfo.nearPlaneMaxBlurInPixels = (NearPlaneMaxBlur / 100.0f) / pixelSizeLength;
	blurInfo.cocFactorPerPixel = pixelSizeLength * blurInfo.farPlaneMaxBlurInPixels;  // not needed for near plane.
	float4 color = PerformNearPlaneDiscBlur(blurInfo, TexColor);
	RWTexOut[DTid] = color;
}

[numthreads(8, 8, 1)] void CS_TentFilter(uint2 DTid
										 : SV_DispatchThreadID) {
	// float4 coord = (0.5f / float4(SharedData::BufferDim.x, SharedData::BufferDim.y, SharedData::BufferDim.x, SharedData::BufferDim.y)) * float4(1, 1, -1, 0);
	float4 average;
	// float2 uv = 2.0f * (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
	// average = TexColor.SampleLevel(LinearSampler, (uv - coord.xy), 0);
	// average += TexColor.SampleLevel(LinearSampler, (uv - coord.wy), 0) * 2;
	// average += TexColor.SampleLevel(LinearSampler, (uv - coord.zy), 0);
	// average += TexColor.SampleLevel(LinearSampler, (uv + coord.zw), 0) * 2;
	// average += TexColor.SampleLevel(LinearSampler, uv, 0) * 4;
	// average += TexColor.SampleLevel(LinearSampler, (uv + coord.xw), 0) * 2;
	// average += TexColor.SampleLevel(LinearSampler, (uv + coord.zy), 0);
	// average += TexColor.SampleLevel(LinearSampler, (uv + coord.wy), 0) * 2;
	// average += TexColor.SampleLevel(LinearSampler, (uv + coord.xy), 0);
	uint4 offset = uint4(1, 1, -1, 0);
	average = TexColor[DTid - offset.xy];
	average += TexColor[DTid - offset.wy] * 2;
	average += TexColor[DTid - offset.zy];
	average += TexColor[DTid + offset.zw] * 2;
	average += TexColor[DTid] * 4;
	average += TexColor[DTid + offset.xw] * 2;
	average += TexColor[DTid + offset.zy];
	average += TexColor[DTid + offset.wy] * 2;
	average += TexColor[DTid + offset.xy];
	average /= 16;
	RWTexOut[DTid] = average;
}

[numthreads(8, 8, 1)] void CS_Combiner(uint2 DTid
										: SV_DispatchThreadID)
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
	// all CoC's > 0.1 are full far fragment, below that, we're going to blend. This avoids shimmering far plane without the need of a
	// 'magic' number to boost up the alpha.
	float blendFactor = (realCoC > 0.1) ? 1 : smoothstep(0, 1, (realCoC / 0.1));
	float4 color;
	color = lerp(originalFragment, farFragment, blendFactor);
	color.rgb = lerp(color.rgb, nearFragment.rgb, nearFragment.a * (NearPlaneMaxBlur != 0));
	color.a = 1.0;
	RWTexOut[DTid] = color;
}

[numthreads(8, 8, 1)] void CS_PostSmoothing1(uint2 DTid
											 : SV_DispatchThreadID) {
	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	RWTexOut[DTid] = PerformFullFragmentGaussianBlur(TexColor, uv, DTid, float2((SharedData::BufferDim.z), 0.0));
}

[numthreads(8, 8, 1)] void CS_PostSmoothing2AndFocusing(uint2 DTid
														: SV_DispatchThreadID)
{
	float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;

	float4 color = PerformFullFragmentGaussianBlur(TexPostSmoothInput, uv, DTid, float2(0.0, (SharedData::BufferDim.w)));
	float4 originalColor = TexColor[DTid];

	float coc = abs(TexCoCInput[DTid].r);
	color.rgb = lerp(originalColor.rgb, color.rgb, saturate(coc < length(SharedData::BufferDim.zw) ? 0 : 4 * coc));

	RWTexOut[DTid] = float4(color.rgb, 1.0f);
}