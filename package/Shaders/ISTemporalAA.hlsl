#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
	float4 Feedback : SV_Target1;
};

#if defined(PSHADER)

Texture2D<float4> currentFrameTex : register(t0);
Texture2D<float4> historyTex : register(t1);
Texture2D<float4> velocityTex : register(t2);
Texture2D<float4> depthTex : register(t3);
Texture2D<float4> maskTex : register(t4);
Texture2D<float4> alphaTex : register(t5);

SamplerState currentFrameSampler : register(s0);
SamplerState historySampler : register(s1);
SamplerState velocitySampler : register(s2);
SamplerState depthSampler : register(s3);
SamplerState maskSampler : register(s4);
SamplerState alphaSampler : register(s5);

cbuffer PerGeometry : register(b2)
{
	float4 TexelSizeParams : packoffset(c0);
	float4 JitterAndRes : packoffset(c1);
	float4 NeighborWeights : packoffset(c2);
	float4 TexelOffset : packoffset(c3);
	float4 BlendParams : packoffset(c4);
	float4 ThresholdParams : packoffset(c5);
};

// Decompiler comparison idiom: cmp(expr) => -(expr), used as a truthy mask in ?: selects.
#define cmp -

#ifdef HDR_OUTPUT
// Internal working space for TAA is PQ/BT2020.
// PQ maps [0, 10000 nits] to [0, 1], so the vanilla 1.001 bracket ceiling is correct —
// nothing in the scene legitimately exceeds 1.0 PQ. This is why PQ avoids the bracket
// collapse that caused halos with the linear BT2020 working space.
float3 ConvertRenderInput(float3 gammaColor)
{
	return DisplayMapping::LinearToPQ(Color::BT709ToBT2020(Color::GammaToLinearSafe(gammaColor)), 10000.0);
}
float3 ConvertRenderOutput(float3 pqColor)
{
	return Color::LinearToGammaSafe(Color::BT2020ToBT709(DisplayMapping::PQtoLinear(pqColor, 10000.0)));
}
// Feedback luma round-trip: feedbackOut.x is read back as history.x next frame.
// Storing raw PQ luma [0,1] in a low-precision RT causes quantization banding in highlights
// because PQ encodes high nit values in the upper portion of the [0,1] range where
// 8/10-bit steps are perceptible. Encoding as game-gamma spreads precision like SDR
// and round-trips cleanly through whatever precision the feedback RT uses.
float EncodeFeedbackLuma(float pqLuma)
{
	// PQ → linear (single channel: luma only, no colour transform needed)
	float linearLuma = DisplayMapping::PQtoLinear(pqLuma.xxx, 10000.0).x;
	return Color::LinearToGammaSafe(linearLuma);
}
float DecodeFeedbackLuma(float gammaLuma)
{
	float linearLuma = Color::GammaToLinearSafe(gammaLuma);
	return DisplayMapping::LinearToPQ(linearLuma.xxx, 10000.0).x;
}
#endif

static const float3 kLumaWeights = float3(0.5, 0.25, 0.25);

/*
 * Channel layout (vanilla decompile — swizzles are load-bearing):
 * - Neighbour taps: .yxz sample; luma via dot(.xzy, kLumaWeights); stored as float4(.xyz=GRB, .w=luma).
 * - center: float4 .x=belowHistC1, .yzw=centre RGB; luma via dot(center.zwy, kLumaWeights).
 * - corner: float4 .xyz=corner GRB, .w=corner luma (depth-guided); .x reused for belowHistA0 mask.
 * - Bracket colours: .yzw holds (R, B, G); .w = luma.
 * - Output colour lives in .yzw (vanilla r3.yzw after blend; colorOut = sampleUV.yzw), not .xyz.
 *
 * Registers are float4 packs — names are semantic but components reuse like the decompile.
 */

float2 ClampScreenUV(float2 screenUV, float2 drMax)
{
	return min(max(FrameBuffer::DynamicResolutionParams1.xy * screenUV, float2(0, 0)), drMax);
}

float4 ClampScreenUV4(float4 screenUV, float2 drMax)
{
	return min(max(FrameBuffer::DynamicResolutionParams1.xyxy * screenUV, float4(0, 0, 0, 0)), drMax.xyxy);
}

float2 ClampHistoryUV(float2 reprojectedUV)
{
	float2 uv = max(FrameBuffer::DynamicResolutionParams1.zw * reprojectedUV, float2(0, 0));
	uv.x = min(FrameBuffer::DynamicResolutionParams2.w, uv.x);
	uv.y = min(FrameBuffer::DynamicResolutionParams1.w, uv.y);
	return uv;
}

float2 GetDynamicResolutionMax()
{
	return float2(FrameBuffer::DynamicResolutionParams2.z, FrameBuffer::DynamicResolutionParams1.y);
}

// Neighbour tap: .yxz sample; luma via dot(.xzy, kLumaWeights). See channel-layout comment above.
struct ISTAA_NeighborTap
{
	float3 grb;
	float luma;
	float belowHist;
};

float3 LoadNeighborGRB(float2 uv)
{
	float3 grb = currentFrameTex.Sample(currentFrameSampler, uv).yxz;
#	ifdef HDR_OUTPUT
	grb.yxz = ConvertRenderInput(grb.yxz);
#	endif
	return grb;
}

ISTAA_NeighborTap SampleNeighborGRB(float2 uv, float historyLuma)
{
	ISTAA_NeighborTap tap;
	tap.grb = LoadNeighborGRB(uv);
	tap.luma = dot(tap.grb.xzy, kLumaWeights);
	tap.belowHist = cmp(tap.luma < historyLuma);
	return tap;
}

float4 PackNeighborTap(ISTAA_NeighborTap tap)
{
	return float4(tap.grb, tap.luma);
}

void AssignPackedNeighbor(float2 uv, float historyLuma, out float4 packed, out float belowHist)
{
	ISTAA_NeighborTap tap = SampleNeighborGRB(uv, historyLuma);
	packed = PackNeighborTap(tap);
	belowHist = tap.belowHist;
}

// Centre tap: .xyz sample into .yzw layout; luma via dot(.zwy, kLumaWeights).
float3 SampleCenterRGB(float2 uv)
{
	float3 rgb = currentFrameTex.Sample(currentFrameSampler, uv).xyz;
#	ifdef HDR_OUTPUT
	rgb = ConvertRenderInput(rgb);
#	endif
	return rgb;
}

float AlphaCoverageMask(float2 uv)
{
	return cmp(0 < alphaTex.Sample(alphaSampler, uv).z);
}

float FlickerLumaContribution(float centerLuma, float neighborLuma)
{
	float d = centerLuma + -neighborLuma;
	d = 0.200000003 + -abs(d);
	return ceil(d);
}

// shallowestDepth must already include depth before calling.
float2 PickIfShallowestUV(float2 selectedUV, float shallowestDepth, float depth, float2 uvIfMatch)
{
	return cmp(shallowestDepth == depth) ? uvIfMatch : selectedUV;
}

// Pick the shallowest-depth UV in the 3x3 neighbourhood (outputs clamped DR UV sets for later taps).
float2 SelectDepthGuidedUV(
	float2 texCoord,
	float2 drMax,
	out float2 drUVMin,
	out float2 drUVMax,
	out float2 drCenter,
	out float4 drNeighborsA,
	out float4 drNeighborsB,
	out float4 drNeighborsC,
	out float3 cornerColorGRB)
{
	float2 uvMin = -TexelOffset.xy + texCoord;
	float2 uvMax = TexelOffset.xy + texCoord;

	drUVMax = ClampScreenUV(uvMax, drMax);
	float depthMaxCorner = depthTex.Sample(depthSampler, drUVMax).x;
	cornerColorGRB = LoadNeighborGRB(drUVMax);

	float4 neighborsA = TexelOffset.xyxy * float4(1, -1, 1, 0) + texCoord.xyxy;
	drNeighborsA = ClampScreenUV4(neighborsA, drMax);
	float depthA0 = depthTex.Sample(depthSampler, drNeighborsA.xy).x;
	float shallowestDepth = min(depthA0, depthMaxCorner);

	drUVMin = ClampScreenUV(uvMin, drMax);
	float depthMinCorner = depthTex.Sample(depthSampler, drUVMin).x;
	shallowestDepth = min(depthMinCorner, shallowestDepth);

	float2 selectedUV = PickIfShallowestUV(uvMax, shallowestDepth, depthMinCorner, uvMin);
	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthA0, neighborsA.xy);

	float4 neighborsB = TexelOffset.xyxy * float4(0, -1, -1, 1) + texCoord.xyxy;
	drNeighborsB = ClampScreenUV4(neighborsB, drMax);
	float depthB0 = depthTex.Sample(depthSampler, drNeighborsB.xy).x;
	shallowestDepth = min(depthB0, shallowestDepth);
	float depthA1 = depthTex.Sample(depthSampler, drNeighborsA.zw).x;
	shallowestDepth = min(depthA1, shallowestDepth);

	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthA1, neighborsA.zw);
	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthB0, neighborsB.xy);

	float4 neighborsC = TexelOffset.xyxy * float4(-1, 0, 0, 1) + texCoord.xyxy;
	drNeighborsC = ClampScreenUV4(neighborsC, drMax);
	float depthC0 = depthTex.Sample(depthSampler, drNeighborsC.xy).x;
	shallowestDepth = min(depthC0, shallowestDepth);
	float depthB1 = depthTex.Sample(depthSampler, drNeighborsB.zw).x;
	shallowestDepth = min(depthB1, shallowestDepth);

	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthB1, neighborsB.zw);
	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthC0, neighborsC.xy);

	drCenter = ClampScreenUV(texCoord, drMax);
	float depthCenter = depthTex.Sample(depthSampler, drCenter).x;
	shallowestDepth = min(depthCenter, shallowestDepth);
	float depthC1 = depthTex.Sample(depthSampler, drNeighborsC.zw).x;
	shallowestDepth = min(depthC1, shallowestDepth);

	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthC1, neighborsC.zw);
	selectedUV = PickIfShallowestUV(selectedUV, shallowestDepth, depthCenter, texCoord);

	return selectedUV;
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float2 texCoord = input.TexCoord;
	float4 colorOut, feedbackOut;

	// float4 packs — component reuse matches vanilla decompile (see header comment).
	float4 motionReject, sampleUV, history, corner, tapMin; // was r0–r4
	float4 tapA0, tapA1, tapB0, tapB1, tapC0, tapC1;       // was r6–r13
	float4 center, centerMeta, bracketMax, weightedColor, mergeScratch, bracketMinReg; // was r14–r19

	float2 drMax = GetDynamicResolutionMax(), drUVMin, drUVMax, drCenter;
	float4 drNeighborsA, drNeighborsB, drNeighborsC;

	motionReject.xy = SelectDepthGuidedUV(
		texCoord,
		drMax,
		drUVMin,
		drUVMax,
		drCenter,
		drNeighborsA,
		drNeighborsB,
		drNeighborsC,
		corner.xyz);

	// --- motion vector and history sample ---
	history.xy = drMax;
	motionReject.xy = velocityTex.Sample(velocitySampler, ClampScreenUV(motionReject.xy, history.xy)).xy;
	motionReject.zw = texCoord.xy + motionReject.xy;
	motionReject.x = sqrt(dot(motionReject.xy, motionReject.xy));
	tapMin.xy = ClampHistoryUV(motionReject.zw);
	history.xyw = historyTex.Sample(historySampler, tapMin.xy).xyz;
#	ifdef HDR_OUTPUT
	// history.x is stored as game-gamma luma (see EncodeFeedbackLuma on write).
	// Decode to PQ luma to match the working space of all neighbour taps.
	// history.y and history.w are motion scalars — do NOT convert them.
	history.x = DecodeFeedbackLuma(history.x);
#	endif
	corner.w = dot(corner.xzy, kLumaWeights);
	motionReject.y = cmp(corner.w < history.x);

	// --- neighbour colour / luma samples ---
	sampleUV.zw = drUVMin;
	sampleUV.xy = drCenter;
	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(sampleUV.zw, history.x);
		tapMin.xyz = tap.grb;
		sampleUV.z = AlphaCoverageMask(sampleUV.zw);
		tapMin.w = tap.luma;
		sampleUV.w = tap.belowHist;
	}
	AssignPackedNeighbor(drNeighborsA.xy, history.x, tapA0, corner.x);
	AssignPackedNeighbor(drNeighborsA.zw, history.x, tapA1, tapMin.x);
	AssignPackedNeighbor(drNeighborsB.xy, history.x, tapB0, tapA0.x);
	AssignPackedNeighbor(drNeighborsB.zw, history.x, tapB1, tapA1.x);
	AssignPackedNeighbor(drNeighborsC.xy, history.x, tapC0, tapB0.x);
	AssignPackedNeighbor(drNeighborsC.zw, history.x, tapC1, center.x);
	center.yzw = SampleCenterRGB(sampleUV.xy);

	// --- centre bracket seed, neighbourhood bracket, flicker, temporal blend (verbatim math) ---
	centerMeta.x = dot(center.zwy, kLumaWeights);
	bracketMax.x = cmp(centerMeta.x < history.x);
	centerMeta.yz = center.yw;
	// Bracket ceiling: 1.001 is just above the maximum PQ value (1.0 = 10000 nits).
	// Nothing in the scene exceeds this, so the ceiling works correctly in PQ working space.
	// (In linear BT2020 this would be wrong — sky/specular exceed 1.0 linear — but PQ is bounded.)
	bracketMax.y = cmp(centerMeta.x < 1.00100005);
	bracketMax.yzw = bracketMax.yyy ? centerMeta.yzx : float3(1.00100005, 1.00100005, 1.00100005);
	bracketMax.yzw = bracketMax.xxx ? float3(1.00100005, 1.00100005, 1.00100005) : bracketMax.yzw;

	weightedColor.x = cmp(tapC1.w < bracketMax.w);
	weightedColor.xyz = weightedColor.xxx ? tapC1.yzw : bracketMax.yzw;
	bracketMax.yzw = center.xxx ? bracketMax.yzw : weightedColor.xyz;

	// --- neighborhood min/max color bracket ---
	weightedColor.xyz = NeighborWeights.zzz * tapC0.yxz;
	weightedColor.xyz = tapB1.yxz * NeighborWeights.www + weightedColor.xyz;
	weightedColor.xyz = tapC1.yxz * NeighborWeights.yyy + weightedColor.xyz;
	weightedColor.xyz = center.yzw * NeighborWeights.xxx + weightedColor.xyz;
	tapB1.x = cmp(tapC0.w < bracketMax.w);
	mergeScratch.xyz = tapB1.xxx ? tapC0.yzw : bracketMax.yzw;
	bracketMax.yzw = tapB0.xxx ? bracketMax.yzw : mergeScratch.xyz;
	tapB1.x = cmp(tapB1.w < bracketMax.w);
	mergeScratch.xyz = tapB1.xxx ? tapB1.yzw : bracketMax.yzw;
	bracketMax.yzw = tapA1.xxx ? bracketMax.yzw : mergeScratch.xyz;
	tapB1.x = cmp(tapB0.w < bracketMax.w);
	mergeScratch.xyz = tapB1.xxx ? tapB0.yzw : bracketMax.yzw;
	bracketMax.yzw = tapA0.xxx ? bracketMax.yzw : mergeScratch.xyz;
	tapB1.x = cmp(tapA1.w < bracketMax.w);
	mergeScratch.xyz = tapB1.xxx ? tapA1.yzw : bracketMax.yzw;
	bracketMax.yzw = tapMin.xxx ? bracketMax.yzw : mergeScratch.xyz;
	tapB1.x = cmp(tapA0.w < bracketMax.w);
	mergeScratch.xyz = tapB1.xxx ? tapA0.yzw : bracketMax.yzw;
	bracketMax.yzw = corner.xxx ? bracketMax.yzw : mergeScratch.xyz;
	tapB1.x = cmp(tapMin.w < bracketMax.w);
	mergeScratch.xyz = tapB1.xxx ? tapMin.yzw : bracketMax.yzw;
	mergeScratch.yzw = sampleUV.www ? bracketMax.yzw : mergeScratch.xyz;
	tapB1.x = cmp(corner.w < mergeScratch.w);
	bracketMinReg.yzw = tapB1.xxx ? corner.yzw : mergeScratch.yzw;
	tapB1.x = cmp(-0.00100000005 < centerMeta.x);
	bracketMax.yzw = tapB1.xxx ? centerMeta.yzx : float3(-0.00100000005, -0.00100000005, -0.00100000005);
	bracketMax.xyz = bracketMax.xxx ? bracketMax.yzw : float3(-0.00100000005, -0.00100000005, -0.00100000005);
	tapB1.x = cmp(bracketMax.z < tapC1.w);
	tapC1.xyz = tapB1.xxx ? tapC1.yzw : bracketMax.xyz;
	tapC1.xyz = center.xxx ? tapC1.xyz : bracketMax.xyz;

	// --- flicker score from neighbor luma spread ---
	tapB1.x = FlickerLumaContribution(centerMeta.x, tapC1.w);
	tapC0.x = cmp(tapC1.z < tapC0.w);
	tapC0.xyz = tapC0.xxx ? tapC0.yzw : tapC1.xyz;
	tapC0.xyz = tapB0.xxx ? tapC0.xyz : tapC1.xyz;
	tapB0.x = FlickerLumaContribution(centerMeta.x, tapC0.w);
	tapC0.w = cmp(tapC0.z < tapB1.w);
	tapC1.xyz = tapC0.www ? tapB1.yzw : tapC0.xyz;
	tapC0.xyz = tapA1.xxx ? tapC1.xyz : tapC0.xyz;
	tapA1.x = FlickerLumaContribution(centerMeta.x, tapB1.w);
	tapB1.y = cmp(tapC0.z < tapB0.w);
	tapB1.yzw = tapB1.yyy ? tapB0.yzw : tapC0.xyz;
	tapB1.yzw = tapA0.xxx ? tapB1.yzw : tapC0.xyz;
	tapA0.x = FlickerLumaContribution(centerMeta.x, tapB0.w);
	tapB0.y = cmp(tapB1.w < tapA1.w);
	tapB0.yzw = tapB0.yyy ? tapA1.yzw : tapB1.yzw;
	tapB0.yzw = tapMin.xxx ? tapB0.yzw : tapB1.yzw;
	tapMin.x = FlickerLumaContribution(centerMeta.x, tapA1.w);
	tapA1.y = cmp(tapB0.w < tapA0.w);
	tapA1.yzw = tapA1.yyy ? tapA0.yzw : tapB0.yzw;
	tapA1.yzw = corner.xxx ? tapA1.yzw : tapB0.yzw;
	corner.x = FlickerLumaContribution(centerMeta.x, tapA0.w);
	tapA0.y = cmp(tapA1.w < tapMin.w);
	tapA0.yzw = tapA0.yyy ? tapMin.yzw : tapA1.yzw;
	tapA0.yzw = sampleUV.www ? tapA0.yzw : tapA1.yzw;
	sampleUV.w = FlickerLumaContribution(centerMeta.x, tapMin.w);
	bracketMinReg.x = tapA0.z;
	tapMin.y = cmp(tapA0.w < corner.w);
	tapMin.yzw = tapMin.yyy ? corner.yzw : tapA0.yzw;
	tapC0.xw = motionReject.yy ? tapMin.yw : tapA0.yw;
	mergeScratch.x = tapMin.z;
	tapC1.xyzw = motionReject.yyyy ? mergeScratch.xyzw : bracketMinReg.xyzw;
	motionReject.y = FlickerLumaContribution(centerMeta.x, corner.w);
	motionReject.y = 4 + -motionReject.y;
	motionReject.y = motionReject.y + -sampleUV.w;
	motionReject.y = motionReject.y + -corner.x;
	motionReject.y = motionReject.y + -tapMin.x;
	motionReject.y = motionReject.y + -tapA0.x;
	motionReject.y = motionReject.y + -tapA1.x;
	motionReject.y = motionReject.y + -tapB0.x;
	motionReject.y = saturate(motionReject.y + -tapB1.x);

	// --- temporal blend, clamp, and sharpen ---
	sampleUV.w = cmp(1 < tapC1.w);
	corner.x = -tapC1.y * 0.25 + tapC1.w;
	corner.x = -tapC1.z * 0.25 + corner.x;
	corner.y = corner.x + corner.x;
	tapC0.z = tapC1.x;
	corner.xzw = tapC1.yzw;
	tapMin.x = -tapC0.x * 0.25 + tapC0.w;
	tapMin.x = -tapC1.x * 0.25 + tapMin.x;
	tapC0.y = tapMin.x + tapMin.x;
	tapMin.x = cmp(tapC0.w < 0);
	tapMin.xyzw = tapMin.xxxx ? corner.xyzw : tapC0.xyzw;
	tapA0.xyzw = sampleUV.wwww ? tapMin.xyzw : corner.xyzw;
	sampleUV.w = max(tapMin.w, history.x);
	tapA1.x = min(sampleUV.w, tapA0.w);
	tapA1.z = tapA0.w;
	tapA1.y = tapMin.w;
	tapB0.z = corner.w;
	tapB0.x = history.x;
	tapB0.y = tapC0.w;
	sampleUV.w = 0.949999988 * history.y;
	motionReject.y = saturate(motionReject.y * 0.25 + sampleUV.w);
	sampleUV.w = cmp(motionReject.y < 0.902499974);
	history.xyz = sampleUV.www ? tapA1.xyz : tapB0.xyz;
	history.yz = history.zx + -history.yy;
	corner.w = cmp(0.00999999978 < history.y);
	history.y = history.z / history.y;
	history.y = corner.w ? history.y : 0.5;
	tapMin.xyz = sampleUV.www ? tapMin.xyz : tapC0.xyz;
	corner.xyz = sampleUV.www ? tapA0.xyz : corner.xyz;
	corner.xyz = corner.xyz + -tapMin.xyz;
	corner.xyz = history.yyy * corner.xyz + tapMin.xyz;

	// --- disocclusion / mask rejection ---
	// motionReject.zw still holds reprojected UV from motion pass; motionReject.x = motion length
	sampleUV.w = min(motionReject.z, motionReject.w);
	motionReject.zw = cmp(motionReject.zw >= float2(1, 1));
	sampleUV.w = cmp(0 >= sampleUV.w);
	motionReject.z = (int)motionReject.z | (int)sampleUV.w;
	motionReject.z = (int)motionReject.w | (int)motionReject.z;
	history.yz = maskTex.Sample(maskSampler, sampleUV.xy).xy;
	motionReject.w = AlphaCoverageMask(sampleUV.xy);
	sampleUV.x = cmp(ThresholdParams.w < history.z);
	motionReject.z = (int)motionReject.z | (int)sampleUV.x;
	sampleUV.xyw = motionReject.zzz ? center.yzw : corner.xyz;
	centerMeta.w = 0;
	history.xw = motionReject.zz ? centerMeta.xw : history.xw;
	corner.xyz = motionReject.zzz ? center.yzw : weightedColor.xyz;
	tapMin.xyz = center.yzw + -corner.xyz;
	motionReject.z = 128 * TexelSizeParams.x;
	tapA0.z = saturate(motionReject.x / motionReject.z);
	motionReject.x = tapA0.z + -history.w;
	// Luma convergence decay: the *20 and *100 constants were tuned for gamma-space luma.
	// PQ is perceptually uniform — a single linear rescale of the PQ diff is accurate
	// across all luminance levels (unlike converting through gamma, which has a varying
	// derivative and overcorrects at bright and dark extremes).
	// Scale factor: 0.05 gamma ≈ 0.020 PQ at mid-scene luminance → factor ≈ 2.5.
#	ifdef HDR_OUTPUT
	motionReject.z = history.x + -centerMeta.x;
	{
		float lumaDiffScaled = abs(motionReject.z) * 0.05;
		history.xw = -lumaDiffScaled.xx * float2(20, 100) + float2(1, 1);
	}
#	else
	motionReject.z = history.x + -centerMeta.x;
	history.xw = -abs(motionReject.xx) * float2(20, 100) + float2(1, 1);
#	endif
	history.xw = max(float2(0, 0), history.xw);
	tapMin.yzw = history.xxx * tapMin.xyz + corner.xyz;
	sampleUV.xyw = -tapMin.yzw + sampleUV.xyw;
	motionReject.x = BlendParams.x + -BlendParams.y;
	motionReject.x = tapA0.z * motionReject.x + BlendParams.y;
	motionReject.x = min(motionReject.x, history.x);
	tapA0.y = history.w * motionReject.y;
	motionReject.y = 0.99000001 + -motionReject.x;
	motionReject.x = tapA0.y * motionReject.y + motionReject.x;
	feedbackOut.yz = tapA0.yz;
#	ifdef HDR_OUTPUT
	tapMin.yzw = max(tapMin.yzw, 0);
	sampleUV.xyw = saturate(motionReject.xxx * sampleUV.xyw + tapMin.yzw);
	// Skip vanilla BlendParams.z/w detail recovery — neighbourhood delta blows up in linear HDR
	// and causes dark bezels / halos on the alpha-aware corner.yzw output path.
	corner.yzw = sampleUV.xyw;
#	else
	sampleUV.xyw = saturate(motionReject.xxx * sampleUV.xyw + tapMin.yzw);

	tapA0.xyz = sampleUV.xyw + -corner.xyz;
	sampleUV.xyw = saturate(tapA0.xyz * BlendParams.zzz + sampleUV.xyw);

	corner.xyz = corner.xyz + -sampleUV.xyw;
	corner.yzw = saturate(BlendParams.www * corner.xyz + sampleUV.xyw);
#	endif

	motionReject.y = motionReject.x * motionReject.z + centerMeta.x;
	motionReject.x = motionReject.x * motionReject.z;
	motionReject.x = cmp(abs(motionReject.x) < 0.00999999978);
	corner.x = motionReject.x ? centerMeta.x : motionReject.y;
	tapMin.x = dot(tapMin.zwy, kLumaWeights);

	// --- alpha-aware output ---
	motionReject.x = AlphaCoverageMask(drNeighborsA.xy);
	motionReject.y = AlphaCoverageMask(drNeighborsA.zw);
	motionReject.x = motionReject.x ? sampleUV.z : 0;
	motionReject.x = motionReject.y ? motionReject.x : 0;
	motionReject.y = AlphaCoverageMask(drNeighborsB.xy);
	motionReject.z = AlphaCoverageMask(drNeighborsB.zw);
	motionReject.x = motionReject.y ? motionReject.x : 0;
	motionReject.x = motionReject.z ? motionReject.x : 0;
	motionReject.y = AlphaCoverageMask(drNeighborsC.xy);
	motionReject.z = AlphaCoverageMask(drNeighborsC.zw);
	motionReject.x = motionReject.y ? motionReject.x : 0;
	motionReject.x = motionReject.z ? motionReject.x : 0;
	motionReject.y = AlphaCoverageMask(drUVMax);
	motionReject.x = motionReject.y ? motionReject.x : 0;
	motionReject.x = motionReject.w ? motionReject.x : 0;
	motionReject.y = cmp(ThresholdParams.w >= history.y);
	motionReject.z = 1 + -history.z;
	motionReject.x = motionReject.y ? motionReject.x : 0;
	sampleUV.xyzw = motionReject.xxxx ? tapMin.xyzw : corner.xyzw;
	colorOut.xyz = sampleUV.yzw;
#	ifdef HDR_OUTPUT
	// Encode PQ luma to game-gamma for feedback RT storage.
	// Storing raw PQ [0,1] in a low-precision RT causes highlight banding because
	// PQ packs high-nit values into the upper range where RT quantization is visible.
	// Game-gamma encoding spreads precision evenly — symmetric with DecodeFeedbackLuma on read.
	feedbackOut.x = EncodeFeedbackLuma(saturate(sampleUV.x * motionReject.z));
#	else
	feedbackOut.x = saturate(sampleUV.x * motionReject.z);
#	endif
	colorOut.w = 1;
	feedbackOut.w = 1;

#	ifdef HDR_OUTPUT
	// colorOut is the display path — convert from PQ/BT2020 working space to game-gamma/BT709.
	// feedbackOut.x was already encoded above; do not modify it here.
	colorOut.xyz = ConvertRenderOutput(colorOut.xyz);
#	endif

	psout.Color = colorOut;
	psout.Feedback = feedbackOut;
	return psout;
}
#endif
