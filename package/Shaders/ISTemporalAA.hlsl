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

static const float3 kLumaWeights = float3(0.5, 0.25, 0.25);

/*
 * Channel layout (vanilla decompile — swizzles are load-bearing):
 * - Neighbour taps: .yxz sample; luma via dot(.xzy, kLumaWeights).
 * - Centre tap: .xyz sample into r14.yzw; luma via dot(r14.zwy, kLumaWeights).
 * - Bracket colours: .yzw holds (R, B, G); luma in .w.
 * - Output: colorOut.xyz = r1.yzw.
 */

#ifdef HDR_OUTPUT
float3 ConvertRenderInput(float3 gammaColor)
{
	float3 linearColor = Color::GammaToLinearSafe(gammaColor);
	linearColor = Color::BT709ToBT2020(linearColor);
	return DisplayMapping::LinearToPQ(linearColor, 10000.0);
}

float3 ConvertRenderOutput(float3 pqColor)
{
	float3 linearColor = DisplayMapping::PQtoLinear(pqColor, 10000.0);
	linearColor = Color::BT2020ToBT709(linearColor);
	return Color::LinearToGammaSafe(linearColor);
}
#endif

float2 ClampScreenUV(float2 screenUV, float2 drMax)
{
	float2 uv = FrameBuffer::DynamicResolutionParams1.xy * screenUV;
	uv = max(float2(0, 0), uv);
	return min(uv, drMax);
}

float4 ClampScreenUV4(float4 screenUV, float2 drMax)
{
	float4 uv = FrameBuffer::DynamicResolutionParams1.xyxy * screenUV;
	uv = max(float4(0, 0, 0, 0), uv);
	return min(uv, drMax.xyxy);
}

float2 ClampHistoryUV(float2 reprojectedUV)
{
	float2 uv = FrameBuffer::DynamicResolutionParams1.zw * reprojectedUV;
	uv = max(float2(0, 0), uv);
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
	out float2 drCenter,
	out float4 drNeighborsA,
	out float4 drNeighborsB,
	out float4 drNeighborsC,
	out float3 cornerColorGRB)
{
	float2 uvMin = -TexelOffset.xy + texCoord;
	float2 uvMax = TexelOffset.xy + texCoord;

	float2 drUVMax = ClampScreenUV(uvMax, drMax);
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

	// Registers r0–r19 are reused below (vanilla layout); do not rename past neighbour setup.
	float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17, r18, r19;

	float2 drMax = GetDynamicResolutionMax();
	float2 drUVMin;
	float2 drCenter;
	float4 drNeighborsA;
	float4 drNeighborsB;
	float4 drNeighborsC;

	r0.xy = SelectDepthGuidedUV(
		texCoord,
		drMax,
		drUVMin,
		drCenter,
		drNeighborsA,
		drNeighborsB,
		drNeighborsC,
		r3.xyz);

	// --- motion vector and history sample ---
	r2.xy = drMax;
	r0.xy = ClampScreenUV(r0.xy, r2.xy);
	r0.xy = velocityTex.Sample(velocitySampler, r0.xy).xy;
	r0.zw = texCoord.xy + r0.xy;
	r0.x = dot(r0.xy, r0.xy);
	r0.x = sqrt(r0.x);
	r4.xy = ClampHistoryUV(r0.zw);
	r2.xyw = historyTex.Sample(historySampler, r4.xy).xyz;
	r3.w = dot(r3.xzy, kLumaWeights);
	r0.y = cmp(r3.w < r2.x);

	// --- neighbour colour / luma samples ---
	r1.zw = drUVMin;
	r5 = drNeighborsA;
	r7 = drNeighborsB;
	r8 = drNeighborsC;
	r1.xy = drCenter;

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r1.zw, r2.x);
		r4.xyz = tap.grb;
		r1.z = AlphaCoverageMask(r1.zw);
		r4.w = tap.luma;
		r1.w = tap.belowHist;
	}

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r5.xy, r2.x);
		r6 = float4(tap.grb, tap.luma);
		r3.x = tap.belowHist;
	}

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r5.zw, r2.x);
		r9 = float4(tap.grb, tap.luma);
		r4.x = tap.belowHist;
	}

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r7.xy, r2.x);
		r10 = float4(tap.grb, tap.luma);
		r6.x = tap.belowHist;
	}

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r7.zw, r2.x);
		r11 = float4(tap.grb, tap.luma);
		r9.x = tap.belowHist;
	}

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r8.xy, r2.x);
		r12 = float4(tap.grb, tap.luma);
		r10.x = tap.belowHist;
	}

	{
		ISTAA_NeighborTap tap = SampleNeighborGRB(r8.zw, r2.x);
		r13 = float4(tap.grb, tap.luma);
		r14.x = tap.belowHist;
	}

	r14.yzw = SampleCenterRGB(r1.xy);

	// --- centre bracket seed, neighbourhood bracket, flicker, temporal blend (verbatim math) ---
	r15.x = dot(r14.zwy, kLumaWeights);
	r16.x = cmp(r15.x < r2.x);
	r15.yz = r14.yw;

	// removing this causes flickering on high contrast edges
	// flickering is even stronger when removing it in PQ
	// won't matter in PQ as 1.0 is already 10k nits
	r16.y = cmp(r15.x < 1.00100005);
	r16.yzw = r16.yyy ? r15.yzx : float3(1.00100005, 1.00100005, 1.00100005);
	r16.yzw = r16.xxx ? float3(1.00100005, 1.00100005, 1.00100005) : r16.yzw;

	r17.x = cmp(r13.w < r16.w);
	r17.xyz = r17.xxx ? r13.yzw : r16.yzw;
	r16.yzw = r14.xxx ? r16.yzw : r17.xyz;

	// --- neighborhood min/max color bracket ---
	r17.xyz = NeighborWeights.zzz * r12.yxz;
	r17.xyz = r11.yxz * NeighborWeights.www + r17.xyz;
	r17.xyz = r13.yxz * NeighborWeights.yyy + r17.xyz;
	r17.xyz = r14.yzw * NeighborWeights.xxx + r17.xyz;
	r11.x = cmp(r12.w < r16.w);
	r18.xyz = r11.xxx ? r12.yzw : r16.yzw;
	r16.yzw = r10.xxx ? r16.yzw : r18.xyz;
	r11.x = cmp(r11.w < r16.w);
	r18.xyz = r11.xxx ? r11.yzw : r16.yzw;
	r16.yzw = r9.xxx ? r16.yzw : r18.xyz;
	r11.x = cmp(r10.w < r16.w);
	r18.xyz = r11.xxx ? r10.yzw : r16.yzw;
	r16.yzw = r6.xxx ? r16.yzw : r18.xyz;
	r11.x = cmp(r9.w < r16.w);
	r18.xyz = r11.xxx ? r9.yzw : r16.yzw;
	r16.yzw = r4.xxx ? r16.yzw : r18.xyz;
	r11.x = cmp(r6.w < r16.w);
	r18.xyz = r11.xxx ? r6.yzw : r16.yzw;
	r16.yzw = r3.xxx ? r16.yzw : r18.xyz;
	r11.x = cmp(r4.w < r16.w);
	r18.xyz = r11.xxx ? r4.yzw : r16.yzw;
	r18.yzw = r1.www ? r16.yzw : r18.xyz;
	r11.x = cmp(r3.w < r18.w);
	r19.yzw = r11.xxx ? r3.yzw : r18.yzw;
	r11.x = cmp(-0.00100000005 < r15.x);
	r16.yzw = r11.xxx ? r15.yzx : float3(-0.00100000005, -0.00100000005, -0.00100000005);
	r16.xyz = r16.xxx ? r16.yzw : float3(-0.00100000005, -0.00100000005, -0.00100000005);
	r11.x = cmp(r16.z < r13.w);
	r13.xyz = r11.xxx ? r13.yzw : r16.xyz;
	r13.xyz = r14.xxx ? r13.xyz : r16.xyz;

	// --- flicker score from neighbor luma spread ---
	r11.x = FlickerLumaContribution(r15.x, r13.w);
	r12.x = cmp(r13.z < r12.w);
	r12.xyz = r12.xxx ? r12.yzw : r13.xyz;
	r12.xyz = r10.xxx ? r12.xyz : r13.xyz;
	r10.x = FlickerLumaContribution(r15.x, r12.w);
	r12.w = cmp(r12.z < r11.w);
	r13.xyz = r12.www ? r11.yzw : r12.xyz;
	r12.xyz = r9.xxx ? r13.xyz : r12.xyz;
	r9.x = FlickerLumaContribution(r15.x, r11.w);
	r11.y = cmp(r12.z < r10.w);
	r11.yzw = r11.yyy ? r10.yzw : r12.xyz;
	r11.yzw = r6.xxx ? r11.yzw : r12.xyz;
	r6.x = FlickerLumaContribution(r15.x, r10.w);
	r10.y = cmp(r11.w < r9.w);
	r10.yzw = r10.yyy ? r9.yzw : r11.yzw;
	r10.yzw = r4.xxx ? r10.yzw : r11.yzw;
	r4.x = FlickerLumaContribution(r15.x, r9.w);
	r9.y = cmp(r10.w < r6.w);
	r9.yzw = r9.yyy ? r6.yzw : r10.yzw;
	r9.yzw = r3.xxx ? r9.yzw : r10.yzw;
	r3.x = FlickerLumaContribution(r15.x, r6.w);
	r6.y = cmp(r9.w < r4.w);
	r6.yzw = r6.yyy ? r4.yzw : r9.yzw;
	r6.yzw = r1.www ? r6.yzw : r9.yzw;
	r1.w = FlickerLumaContribution(r15.x, r4.w);
	r19.x = r6.z;
	r4.y = cmp(r6.w < r3.w);
	r4.yzw = r4.yyy ? r3.yzw : r6.yzw;
	r12.xw = r0.yy ? r4.yw : r6.yw;
	r18.x = r4.z;
	r13.xyzw = r0.yyyy ? r18.xyzw : r19.xyzw;
	r0.y = FlickerLumaContribution(r15.x, r3.w);
	r0.y = 4 + -r0.y;
	r0.y = r0.y + -r1.w;
	r0.y = r0.y + -r3.x;
	r0.y = r0.y + -r4.x;
	r0.y = r0.y + -r6.x;
	r0.y = r0.y + -r9.x;
	r0.y = r0.y + -r10.x;
	r0.y = saturate(r0.y + -r11.x);

	// --- temporal blend, clamp, and sharpen ---
	r1.w = cmp(1 < r13.w);
	r3.x = -r13.y * 0.25 + r13.w;
	r3.x = -r13.z * 0.25 + r3.x;
	r3.y = r3.x + r3.x;
	r12.z = r13.x;
	r3.xzw = r13.yzw;
	r4.x = -r12.x * 0.25 + r12.w;
	r4.x = -r13.x * 0.25 + r4.x;
	r12.y = r4.x + r4.x;
	r4.x = cmp(r12.w < 0);
	r4.xyzw = r4.xxxx ? r3.xyzw : r12.xyzw;
	r6.xyzw = r1.wwww ? r4.xyzw : r3.xyzw;
	r1.w = max(r4.w, r2.x);
	r9.x = min(r1.w, r6.w);
	r9.z = r6.w;
	r9.y = r4.w;
	r10.z = r3.w;
	r10.x = r2.x;
	r10.y = r12.w;
	r1.w = 0.949999988 * r2.y;
	r0.y = saturate(r0.y * 0.25 + r1.w);
	r1.w = cmp(r0.y < 0.902499974);
	r2.xyz = r1.www ? r9.xyz : r10.xyz;
	r2.yz = r2.zx + -r2.yy;
	r3.w = cmp(0.00999999978 < r2.y);
	r2.y = r2.z / r2.y;
	r2.y = r3.w ? r2.y : 0.5;
	r4.xyz = r1.www ? r4.xyz : r12.xyz;
	r3.xyz = r1.www ? r6.xyz : r3.xyz;
	r3.xyz = r3.xyz + -r4.xyz;
	r3.xyz = r2.yyy * r3.xyz + r4.xyz;

	// --- disocclusion / mask rejection ---
	// r0.zw still holds reprojected UV from motion pass; r0.x = motion length
	r1.w = min(r0.z, r0.w);
	r0.zw = cmp(r0.zw >= float2(1, 1));
	r1.w = cmp(0 >= r1.w);
	r0.z = (int)r0.z | (int)r1.w;
	r0.z = (int)r0.w | (int)r0.z;
	r2.yz = maskTex.Sample(maskSampler, r1.xy).xy;
	r0.w = AlphaCoverageMask(r1.xy);
	r1.x = cmp(ThresholdParams.w < r2.z);
	r0.z = (int)r0.z | (int)r1.x;
	r1.xyw = r0.zzz ? r14.yzw : r3.xyz;
	r15.w = 0;
	r2.xw = r0.zz ? r15.xw : r2.xw;
	r3.xyz = r0.zzz ? r14.yzw : r17.xyz;
	r4.xyz = r14.yzw + -r3.xyz;
	r0.z = 128 * TexelSizeParams.x;
	r6.z = saturate(r0.x / r0.z);
	r0.x = r6.z + -r2.w;
	r0.z = r2.x + -r15.x;
	r2.xw = -abs(r0.xx) * float2(20, 100) + float2(1, 1);
	r2.xw = max(float2(0, 0), r2.xw);
	r4.yzw = r2.xxx * r4.xyz + r3.xyz;
	r1.xyw = -r4.yzw + r1.xyw;
	r0.x = BlendParams.x + -BlendParams.y;
	r0.x = r6.z * r0.x + BlendParams.y;
	r0.x = min(r0.x, r2.x);
	r6.y = r2.w * r0.y;
	r0.y = 0.99000001 + -r0.x;
	r0.x = r6.y * r0.y + r0.x;
	feedbackOut.yz = r6.yz;
#	ifdef HDR_OUTPUT
	r1.xyw = (r0.xxx * r1.xyw + r4.yzw);
#	else
	r1.xyw = saturate(r0.xxx * r1.xyw + r4.yzw);
#	endif

	r6.xyz = r1.xyw + -r3.xyz;
#	ifdef HDR_OUTPUT
	r1.xyw = (r6.xyz * BlendParams.zzz + r1.xyw);
#	else
	r1.xyw = saturate(r6.xyz * BlendParams.zzz + r1.xyw);
#	endif

	r3.xyz = r3.xyz + -r1.xyw;
#	ifdef HDR_OUTPUT
	r3.yzw = (BlendParams.www * r3.xyz + r1.xyw);
#	else
	r3.yzw = saturate(BlendParams.www * r3.xyz + r1.xyw);
#	endif

	r0.y = r0.x * r0.z + r15.x;
	r0.x = r0.x * r0.z;
	r0.x = cmp(abs(r0.x) < 0.00999999978);
	r3.x = r0.x ? r15.x : r0.y;
	r4.x = dot(r4.zwy, kLumaWeights);

	// --- alpha-aware output ---
	r0.x = AlphaCoverageMask(r5.xy);
	r0.y = AlphaCoverageMask(r5.zw);
	r0.x = r0.x ? r1.z : 0;
	r0.x = r0.y ? r0.x : 0;
	r0.y = AlphaCoverageMask(r7.xy);
	r0.z = AlphaCoverageMask(r7.zw);
	r0.x = r0.y ? r0.x : 0;
	r0.x = r0.z ? r0.x : 0;
	r0.y = AlphaCoverageMask(r8.xy);
	r0.z = AlphaCoverageMask(r8.zw);
	r0.x = r0.y ? r0.x : 0;
	r0.x = r0.z ? r0.x : 0;
	r0.x = r0.w ? r0.x : 0;
	r0.y = cmp(ThresholdParams.w >= r2.y);
	r0.z = 1 + -r2.z;
	r0.x = r0.y ? r0.x : 0;
	r1.xyzw = r0.xxxx ? r4.xyzw : r3.xyzw;
	colorOut.xyz = r1.yzw;
#	ifdef HDR_OUTPUT
	feedbackOut.x = (r1.x * r0.z);
#	else
	feedbackOut.x = saturate(r1.x * r0.z);
#	endif
	colorOut.w = 1;
	feedbackOut.w = 1;

#	ifdef HDR_OUTPUT
	feedbackOut.x = max(0, feedbackOut.x);
	colorOut.xyz = ConvertRenderOutput(colorOut.xyz);
#	endif

	psout.Color = colorOut;
	psout.Feedback = feedbackOut;
	return psout;
}
#endif
