#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/VR.hlsli"

namespace TAA
{
	float3 SaturateSDR(float3 color)
	{
#ifdef HDR_OUTPUT
		return color;
#else
		return saturate(color);
#endif
	}
}

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
	// Feedback buffer — read back next frame via historyTex.xyz:
	//   .x = lumaFeedback   (outputLuma × (1−mask); drives historyLuma next frame)
	//   .y = historyFlicker (flickerFactor accumulated with decay; drives bracket stability)
	//   .z = prevMotion     (SE only: normalizedMotion; drives motionDiff next frame. VR writes 0.)
	//   .w = 1.0
	float4 Feedback: SV_Target1;
};

#if defined(PSHADER)

// Textures
Texture2D<float4> currentFrameTex : register(t0);  // Main render (current frame)
Texture2D<float4> historyTex : register(t1);       // History scalars (lumaFeedback, historyFlicker, prevMotion)
Texture2D<float4> velocityTex : register(t2);      // Motion vector / velocity buffer
Texture2D<float4> depthTex : register(t3);         // Scene depth buffer
Texture2D<float4> maskTex : register(t4);          // TAA validity / mask buffer
Texture2D<float4> alphaTex : register(t5);         // Alpha / transparency buffer

// Samplers
SamplerState currentFrameSampler : register(s0);
SamplerState historySampler : register(s1);
SamplerState velocitySampler : register(s2);
SamplerState depthSampler : register(s3);
SamplerState maskSampler : register(s4);
SamplerState alphaSampler : register(s5);

// Per-pass TAA constants.
cbuffer PerGeometry : register(b2)
{
	float4 TexelSizeParams : packoffset(c0);  // xy = texel size (1/W, 1/H), zw = 1.0
	float4 JitterAndRes : packoffset(c1);     // xy = jitter, zw = render resolution
	float4 NeighborWeights : packoffset(c2);  // xyzw = 4-tap weighted average weights
	float4 TexelOffset : packoffset(c3);      // xy = texel size (same as c0.xy)
	float4 BlendParams : packoffset(c4);      // x = minBlend, y = maxBlend, z = sharpenA, w = sharpenB
	float4 ThresholdParams : packoffset(c5);  // w = depth rejection threshold
};

namespace TAA
{
	// Neighbor tap indices within the 9-tap grid (row-major, 3x3).
	// 0=UL, 1=U,  2=RU,
	// 3=L,  4=C,  5=R,
	// 6=LD, 7=D,  8=BR
	static const float2 NeighborOffsets[9] = {
		float2(-1, -1),  // 0: UL
		float2(0, -1),   // 1: U
		float2(1, -1),   // 2: RU
		float2(-1, 0),   // 3: L
		float2(0, 0),    // 4: C (center)
		float2(1, 0),    // 5: R
		float2(-1, 1),   // 6: LD
		float2(0, 1),    // 7: D
		float2(1, 1),    // 8: BR
	};

	namespace Constants
	{
		// Algorithm Magic Constants (Derived from Decompile)
		// =========================================================================

		// Luma approximations and constraints
		static const float MaxLumaCap = 1.00100005;
		static const float MinLumaCap = -0.00100000005;

		// Flicker detection scaling
		static const float FlickerLumaThreshold = 0.2;
		static const float FlickerDecay = 0.95;
		static const float FlickerWeightScale = 0.25;
		static const float ClampBlendThreshold = 0.902499974;  // Approx 0.95^2

		// Motion resolution bounds
		static const float MaxMotionMagnitudePixels = 128.0;
		static const float MotionSimilarityScale = 20.0;
		static const float StrictSimilarityScale = 100.0;
		static const float BlendFloorScale = 0.99;

		// VR sky detection bounds
		static const float SkyDepthThresholdSq = 0.95;
		static const float SkyBlendScale = 20.0;

		// Luma logic and thresholds
		static const float3 LumaWeights = float3(0.25, 0.5, 0.25);
		static const float LumaThreshold = 0.01;
		static const float LumaRatioFallback = 0.5;

		// Flicker score initial value
		static const float FlickerMaxScore = 4.0;

		// =========================================================================
		// Engine / CBuffer Semantic Aliases
		// =========================================================================

		// Texel parameters
		static const float InvWidth = TexelSizeParams.x;
		static const float InvHeight = TexelSizeParams.y;
		static const float2 TexelSize = TexelSizeParams.xy;

		// Blend & Sharpen settings
		static const float MinBlend = BlendParams.x;
		static const float MaxBlend = BlendParams.y;
		static const float SharpenA = BlendParams.z;
		static const float SharpenB = BlendParams.w;

		// Rejection thresholds
		static const float DepthRejectionThreshold = ThresholdParams.w;
	}

	// TAA-specific luma approximation matching Bethesda's decompile weights (not ITU-R BT.709).
	// Bethesda's native math: G=0.5, R=0.25, B=0.25. Input from SampleCurrentFrame is standard x=R, y=G, z=B.
	float Luma(float3 rgb)
	{
		return dot(rgb, Constants::LumaWeights);
	}
}

// Sample current frame natively. HDR output conversions strictly require correct RGB channel ordering.
float3 SampleCurrentFrame(float2 uv)
{
	float3 color = currentFrameTex.Sample(currentFrameSampler, uv).xyz;  // Native RGB layout
#	ifdef HDR_OUTPUT
	color = DisplayMapping::ConvertGameToPQ(color);
#	endif
	return color;
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float2 uv = input.TexCoord;

	// =========================================================================
	// Sample all 9 neighborhood positions (3x3 grid around current pixel).
	// =========================================================================

	float2 uvs[9];
	float depths[9];
	float3 colors[9];
	float lumas[9];

	[unroll] for (int sampleIdx = 0; sampleIdx < 9; sampleIdx++)
	{
		uvs[sampleIdx] = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(TAA::NeighborOffsets[sampleIdx] * TexelOffset.xy + uv);
		depths[sampleIdx] = depthTex.Sample(depthSampler, uvs[sampleIdx]).x;
		colors[sampleIdx] = SampleCurrentFrame(uvs[sampleIdx]);
		lumas[sampleIdx] = TAA::Luma(colors[sampleIdx]);
	}

	// =========================================================================
	// Phase 1: Closest-depth search — find the foreground pixel in the 3x3 grid.
	//
	// Using the foreground motion vector prevents background vectors ghosting
	// onto foreground edges ("disocclusion ghosting").
	// =========================================================================

	int bestIdx = 0;
	float minDepth = depths[0];

	[unroll] for (int depthIdx = 1; depthIdx < 9; depthIdx++)
	{
		if (depths[depthIdx] < minDepth) {
			minDepth = depths[depthIdx];
			bestIdx = depthIdx;
		}
	}

	// =========================================================================
	// Phase 2: Motion reprojection + history scalars
	// =========================================================================

	float2 velocity = velocityTex.Sample(velocitySampler, uvs[bestIdx]).xy;
	float motionMagnitude = sqrt(dot(velocity, velocity));

	// Apply velocity keeping stereoscopic boundaries strictly isolated per eye
	bool prevUVOutOfBounds;
	float2 prevUVunclamped = Stereo::ApplyVelocityToUV(uv, velocity, prevUVOutOfBounds);
	float2 prevUV = FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(prevUVunclamped);

	// historyTex (t1) carries THREE scalar values from the previous frame's Feedback output:
	//   .x = historyLuma    (lumaFeedback: outputLuma × (1−mask))
	//   .y = historyFlicker (flickerFactor accumulated with 0.95 decay)
	//   .z = prevMotion     (SE only: normalizedMotion. VR always 0.)
	float3 historyData = historyTex.Sample(historySampler, prevUV).xyz;
	float historyLuma = historyData.x;
	float historyFlicker = historyData.y;
	float prevMotion = historyData.z;

	// =========================================================================
	// Phase 3: Weighted neighborhood color
	// =========================================================================

	float normalizedMotion = saturate(motionMagnitude / (TAA::Constants::MaxMotionMagnitudePixels * TAA::Constants::InvWidth));

	// 4-tap weighted blend: C + D + L + LD (NeighborWeights.xyzw maps to C, D, L, LD).
	float3 neighborColor =
		NeighborWeights.x * colors[4] +  // C
		NeighborWeights.y * colors[7] +  // D
		NeighborWeights.z * colors[3] +  // L
		NeighborWeights.w * colors[6];   // LD

	// =========================================================================
	// Phase 4: Luminance-bounded AABB history clamping
	//
	// Builds two color brackets from the 8 non-center neighbors to constrain output:
	//   maxBracket: neighbor with luma in [historyLuma, 1.001) — nearest brighter than history
	//   minBracket: neighbor with luma in (-0.001, historyLuma) — nearest darker than history
	//
	// Also accumulates flickerFactor: counts neighbors with luma within ±0.2 of center.
	// More similar neighbors = more stable pixel → flickerFactor grows → tighter temporal anchor.
	// =========================================================================

	float lumaC = lumas[4];

	bool centerBelowHistory = lumaC < historyLuma;
	bool centerAboveHistory = lumaC > historyLuma;

	float maxLuma = centerBelowHistory ? TAA::Constants::MaxLumaCap : min(lumaC, TAA::Constants::MaxLumaCap);
	float3 maxBracket = centerBelowHistory ? TAA::Constants::MaxLumaCap.xxx : colors[4];

	float minLuma = centerAboveHistory ? TAA::Constants::MinLumaCap : max(lumaC, TAA::Constants::MinLumaCap);
	float3 minBracket = centerAboveHistory ? TAA::Constants::MinLumaCap.xxx : colors[4];

	// Count neighbors with luma similar to center (within FlickerLumaThreshold): stable regions score high,
	// edges/flickering regions score low. Matches VR decompile: saturate(FlickerMaxScore - #similar_neighbors).
	float flickerFactor = TAA::Constants::FlickerMaxScore;

	// Walk all 8 non-center neighbors (0–3, 5–8); center is handled via initialization only.
	// BR (index 8) is included — do not reduce this bound to < 8.
	[unroll] for (int neighborIdx = 0; neighborIdx < 9; neighborIdx++)
	{
		if (neighborIdx == 4)
			continue;  // center is initialization-only

		float luma = lumas[neighborIdx];
		float3 color = colors[neighborIdx];

		if (luma < maxLuma && luma >= historyLuma) {
			maxBracket = color;
			maxLuma = luma;
		}
		if (luma > minLuma && luma < historyLuma) {
			minBracket = color;
			minLuma = luma;
		}

		// Each neighbor with |luma - center| < FlickerLumaThreshold decrements the flicker factor.
		flickerFactor -= ceil(TAA::Constants::FlickerLumaThreshold - abs(luma - lumaC));
	}
	flickerFactor = saturate(flickerFactor);

	// blendFactor_base: high at edges/flickering pixels, low in flat stable regions.
	// FlickerDecay preserves temporal continuity across frames.
	float blendFactor_base = saturate(flickerFactor * TAA::Constants::FlickerWeightScale + TAA::Constants::FlickerDecay * historyFlicker);

	// Sentinel collapse: collapsed bracket values are computed unconditionally,
	// then selected between collapsed and pre-collapse based on blendFactor_base.
	//
	// minSentinel: minLuma stayed at MinLumaCap (<0). Fires when centerAboveHistory AND no
	// neighbor had luma in (-0.001, historyLuma) — rare but possible in high-contrast regions.
	// Vanilla handles it defensively by swapping min/max brackets.
	// maxSentinel: maxLuma stayed at MaxLumaCap (>1). Fires when centerBelowHistory AND no
	// neighbor had luma >= historyLuma — the ghost-pixel trigger case.
	bool minSentinel = minLuma < 0.0;
	float3 minBracket_adj = minSentinel ? maxBracket : minBracket;
	float minLuma_adj = minSentinel ? maxLuma : minLuma;
	bool maxSentinel = maxLuma >= TAA::Constants::MaxLumaCap;
	float3 maxBracket_collapsed = maxSentinel ? minBracket_adj : maxBracket;
	float3 minBracket_collapsed = minBracket_adj;
	float maxLuma_collapsed = maxSentinel ? minLuma_adj : maxLuma;
	float minLuma_collapsed = minLuma_adj;
	float effectiveHistLuma_collapsed = min(max(minLuma_collapsed, historyLuma), maxLuma_collapsed);

	// Select collapsed vs pre-collapse based on blendFactor_base:
	//   < 0.9025 (stable pixel)    → collapsed bracket + clamped historyLuma (prevents ghosting)
	//   >= 0.9025 (flickery pixel) → pre-collapse bracket + raw historyLuma (trusts history)
	bool useClamped = blendFactor_base < TAA::Constants::ClampBlendThreshold;
	float effectiveHistoryLuma = useClamped ? effectiveHistLuma_collapsed : historyLuma;
	float3 maxBracket_sel = useClamped ? maxBracket_collapsed : maxBracket;
	float3 minBracket_sel = useClamped ? minBracket_collapsed : minBracket;
	float maxLuma_sel = useClamped ? maxLuma_collapsed : maxLuma;
	float minLuma_sel = useClamped ? minLuma_collapsed : minLuma;

	// Interpolate within selected bracket using effectiveHistoryLuma as position reference.
	// Vanilla raw uses unsaturated division with 0.5 fallback (no saturate on the ratio itself).
	float lumaRange = maxLuma_sel - minLuma_sel;
	float lumaRatio = (lumaRange > TAA::Constants::LumaThreshold) ? (effectiveHistoryLuma - minLuma_sel) / lumaRange : TAA::Constants::LumaRatioFallback;
	float3 bracketColor = lerp(minBracket_sel, maxBracket_sel, lumaRatio);

	// Mask sample and reject flag are computed here (before Phase 5) so the reject state
	// is available inside the VR neighborBlend computation.
	float2 maskSample = maskTex.Sample(maskSampler, uvs[4]).xy;
	float maskGate = maskSample.x;    // allTransparent gate: DepthRejectionThreshold >= maskGate
	float maskReject = maskSample.y;  // reject threshold; also scales lumaFeedback via (1 - maskReject)
	bool reject = prevUVOutOfBounds || TAA::Constants::DepthRejectionThreshold < maskReject;

	// =========================================================================
	// Phase 5: Temporal blending
	//
	// VR:  blendFactor = sky detector only — saturate(20*(minDepth²−0.95)).
	//      Regular objects: blendFactor = 0, output = neighborColor (current frame only).
	//      Sky (minDepth≥0.975): blendFactor > 0, output blends toward bracketColor.
	//      neighborBlend interpolates between neighborColor and center based on skyFactor.
	//      When reject is true, neighborBase = centerColor so neighborBlend = centerColor.
	//
	// SE:  blendFactor = strictFeedback × (0.99 − motionBlend), no constant floor.
	//      motionSimilarity (×20 decay) and strictSimilarity (×100 decay) are computed
	//      from motionDiff vs the previous frame's normalizedMotion (t1.z = prevMotion).
	//      neighborBlend interpolates between neighborColor and center based on motionSimilarity.
	// =========================================================================

	float3 neighborBlend;
	float blendFactor;
	float strictFeedback = 0.0;

#	ifdef VR
	// VR: sky-detection blend factor.
	// minDepth < ~0.975 for regular objects → skyFactor = 0 → output = neighborColor.
	// minDepth ≥ ~0.975 for sky/background → skyFactor > 0 → blend toward bracketColor.
	float skyFactor = saturate(TAA::Constants::SkyBlendScale * (minDepth * minDepth - TAA::Constants::SkyDepthThresholdSq));
	float3 neighborBase = reject ? colors[4] : neighborColor;
	neighborBlend = skyFactor * (colors[4] - neighborBase) + neighborBase;
	blendFactor = min(TAA::Constants::MaxBlend, skyFactor);
#	else
	// SE: motion-continuity blend factor.
	// motionSimilarity/strictSimilarity: how closely current motion matches previous frame.
	float motionDiff = normalizedMotion - prevMotion;
	float motionSimilarity = max(0.0, 1.0 - TAA::Constants::MotionSimilarityScale * abs(motionDiff));
	float strictSimilarity = max(0.0, 1.0 - TAA::Constants::StrictSimilarityScale * abs(motionDiff));
	// motionBlend: lerp from maxBlend→minBlend with motion, then clamped by motionSimilarity.
	float motionBlend = min(lerp(TAA::Constants::MaxBlend, TAA::Constants::MinBlend, normalizedMotion), motionSimilarity);
	neighborBlend = motionSimilarity * (colors[4] - neighborColor) + neighborColor;
	strictFeedback = strictSimilarity * blendFactor_base;
	// blendFactor has a motionBlend floor: ensures at least motionBlend-worth of temporal mixing
	// even in perfectly stable regions.
	blendFactor = strictFeedback * (TAA::Constants::BlendFloorScale - motionBlend) + motionBlend;
#	endif

	// Non-HDR path saturates each stage (raw wraps all three in saturate() outside
	// #ifdef HDR_OUTPUT). HDR path leaves values unclamped so the PQ range survives.
	// Final lerp is toward neighborColor (unsharpened current-frame mix), not toward blended.

	float3 blended = TAA::SaturateSDR(blendFactor * (bracketColor - neighborBlend) + neighborBlend);
	float3 sharpened = TAA::SaturateSDR(blended + (blended - neighborColor) * TAA::Constants::SharpenA);
	float3 finalColor = TAA::SaturateSDR(lerp(sharpened, neighborColor, TAA::Constants::SharpenB));

	// =========================================================================
	// Phase 6: History rejection
	//
	// maskTex.x gates allTransparent; maskTex.y is the rejection channel.
	// mask sample and reject flag were computed before Phase 5 — see above.
	// =========================================================================

	// If all 8 non-BR neighbors are transparent (glass/water), bypass TAA —
	// but only when the mask gate permits it (DepthRejectionThreshold >= maskGate). Masked
	// regions always run the regular blend path regardless of alpha.
	// BR (index 8) is intentionally excluded here — loop bound < 8 is correct for alpha.
	bool allTransparent = TAA::Constants::DepthRejectionThreshold >= maskGate;
	[unroll] for (int alphaIdx = 0; alphaIdx < 8; alphaIdx++)
	{
		allTransparent = allTransparent && (alphaTex.Sample(alphaSampler, uvs[alphaIdx]).z > 0);
	}

	float3 outputColor = reject ? colors[4] : finalColor;
	// allTransparent path outputs neighborBlend (not center) with alpha=0.
	float3 finalOutput = allTransparent ? neighborBlend : outputColor;

#	ifdef HDR_OUTPUT
	finalOutput = DisplayMapping::ConvertPQToGame(finalOutput);
#	endif

	// lumaFeedback anchors on lumaC (center luma), not Luma(finalColor).
	// effectiveHistoryLuma was computed in Phase 4 (sentinel collapse + bfb gate).
	// Collapse to lumaC when rejected so history decays cleanly.
	// rawLuma = lumaC + blendFactor * (effectiveHistoryLuma - lumaC)
	// (epsilon-guarded: |correction| < LumaThreshold ⇒ rawLuma = lumaC)
	// allTransparent path uses Luma(neighborBlend) instead.
	effectiveHistoryLuma = reject ? lumaC : effectiveHistoryLuma;
	float bf_correction = blendFactor * (effectiveHistoryLuma - lumaC);
	float rawLuma = (abs(bf_correction) < TAA::Constants::LumaThreshold) ? lumaC : (lumaC + bf_correction);
	float lumaFeedback = (allTransparent ? TAA::Luma(neighborBlend) : rawLuma) * (1.0 - maskReject);

	// =========================================================================
	// Phase 7: Output Packaging
	// =========================================================================

#	ifdef VR
	// VR uses alpha=0 for transparent surfaces to signal downstream compositing.
	float outAlpha = allTransparent ? 0.0 : 1.0;
	// VR historyFlicker: zeroed when minDepth >= 1.0 (sky/void pixels get no temporal anchor).
	// ceil(minDepth - (1.0 - EPSILON_DEPTH_SKY)) = 1 when minDepth >= 1.0, 0 otherwise → 1 - that = gate.
	float outFlicker = saturate(blendFactor_base * (1.0 - ceil(minDepth - (1.0f - EPSILON_DEPTH_SKY))));
	float outMotion = 0.0;  // VR does not use prevMotion.
#	else
	// SE always writes alpha=1.
	float outAlpha = 1.0;
	float outFlicker = saturate(strictFeedback);  // historyFlicker for next frame
	float outMotion = normalizedMotion;           // prevMotion: drives motionDiff next frame
#	endif

#	ifdef HDR_OUTPUT
	float outLuma = max(0.0, lumaFeedback);
#	else
	float outLuma = saturate(lumaFeedback);
#	endif

	psout.Color = float4(finalOutput, outAlpha);
	psout.Feedback = float4(outLuma, outFlicker, outMotion, 1.0);

	return psout;
}
#endif
