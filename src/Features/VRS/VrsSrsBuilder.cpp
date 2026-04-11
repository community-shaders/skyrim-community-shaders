#include "VrsSrsBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	// Ring rate sequences per preset.  Each defines the shading rate
	// progression from foveal center outward; last entry repeats for
	// all rings beyond array length.

	// Default: 6-step gradual with directional half/eighth rates.
	static constexpr RingRate kDefaultSeq[] = {
		RingRate::Rate_1x1,
		RingRate::Rate_Half,
		RingRate::Rate_2x2,
		RingRate::Rate_Eighth,
		RingRate::Rate_4x4,
		RingRate::Rate_Cull,
	};

	// Faster: 4-step symmetric, no directional split.
	static constexpr RingRate kFasterSeq[] = {
		RingRate::Rate_1x1,
		RingRate::Rate_2x2,
		RingRate::Rate_4x4,
		RingRate::Rate_Cull,
	};

	// Extreme: 3-step, native → coarsest → cull.
	static constexpr RingRate kExtremeSeq[] = {
		RingRate::Rate_1x1,
		RingRate::Rate_4x4,
		RingRate::Rate_Cull,
	};

	// Boundary dither: checkerboard pattern at ring boundaries to soften transitions.
	constexpr float kDitherFraction = 0.15f;

	// Debug visualisation: RGBA colour per SrsLevel.
	constexpr uint32_t kDebugColors[SrsLevel::kCount] = {
		0xFF59D933u,  // k1x1:  green
		0xFFD9BF33u,  // k2x1:  cyan
		0xFFBFD933u,  // k1x2:  teal
		0xFF33CCF2u,  // k2x2:  yellow
		0xFF268CFAu,  // k4x2:  orange
		0xFF4D73F2u,  // k2x4:  salmon
		0xFF3838DBu,  // k4x4:  red
		0xFF4D4D4Du,  // kCull: dark grey
	};
}

// --- Static helpers ---

void VrsSrsBuilder::GetRingRateSequence(uint32_t preset, const RingRate*& out, uint32_t& count)
{
	switch (preset) {
	case 1:
		out = kFasterSeq;
		count = static_cast<uint32_t>(std::size(kFasterSeq));
		break;
	case 2:
		out = kExtremeSeq;
		count = static_cast<uint32_t>(std::size(kExtremeSeq));
		break;
	case 0:
	default:
		out = kDefaultSeq;
		count = static_cast<uint32_t>(std::size(kDefaultSeq));
		break;
	}
}

uint8_t VrsSrsBuilder::ResolveRingLevel(RingRate rate, float dx, float dy, float halfW, float halfH, bool directional)
{
	switch (rate) {
	case RingRate::Rate_1x1:
		return SrsLevel::k1x1;
	case RingRate::Rate_Half:
		if (directional) {
			// Directional half-rate: tiles near horizontal axis use 2×1 (coarsen
			// vertically, preserve the horizontal detail human eyes track along);
			// tiles near vertical axis use 1×2 (opposite).
			const bool horizontal = std::abs(dy) * halfW < std::abs(dx) * halfH;
			return horizontal ? SrsLevel::k2x1 : SrsLevel::k1x2;
		}
		return SrsLevel::k2x1;
	case RingRate::Rate_2x2:
		return SrsLevel::k2x2;
	case RingRate::Rate_Eighth:
		if (directional) {
			// Same directional logic at eighth-rate: 4×2 horizontal / 2×4 vertical.
			const bool horizontal = std::abs(dy) * halfW < std::abs(dx) * halfH;
			return horizontal ? SrsLevel::k4x2 : SrsLevel::k2x4;
		}
		return SrsLevel::k4x2;
	case RingRate::Rate_4x4:
		return SrsLevel::k4x4;
	case RingRate::Rate_Cull:
	default:
		return SrsLevel::kCull;
	}
}

void VrsSrsBuilder::BuildDebugVisualization(uint32_t* dst, const uint8_t* src, uint32_t width, uint32_t height)
{
	const uint32_t count = width * height;
	for (uint32_t i = 0; i < count; ++i) {
		const uint8_t level = src[i];
		dst[i] = (level < SrsLevel::kCount) ? kDebugColors[level] : kDebugColors[SrsLevel::kCull];
	}
}

// --- NeedsRebuild ---

bool VrsSrsBuilder::NeedsRebuild(uint32_t width, uint32_t height, uint32_t renderWidth, uint32_t renderHeight, const Params& params) const
{
	if (!hasCache)
		return true;
	if (width != lastWidth || height != lastHeight || renderWidth != lastRenderWidth || renderHeight != lastRenderHeight)
		return true;
	if (params.srsPreset != lastParams.srsPreset)
		return true;
	if (params.enableDirectionalRates != lastParams.enableDirectionalRates ||
		params.enableBoundaryDither != lastParams.enableBoundaryDither)
		return true;
	if (params.leftSubrectUV.x != lastParams.leftSubrectUV.x ||
		params.leftSubrectUV.y != lastParams.leftSubrectUV.y ||
		params.leftSubrectUV.w != lastParams.leftSubrectUV.w ||
		params.leftSubrectUV.h != lastParams.leftSubrectUV.h ||
		params.rightSubrectUV.x != lastParams.rightSubrectUV.x ||
		params.rightSubrectUV.y != lastParams.rightSubrectUV.y ||
		params.rightSubrectUV.w != lastParams.rightSubrectUV.w ||
		params.rightSubrectUV.h != lastParams.rightSubrectUV.h ||
		params.ringGrowthRate != lastParams.ringGrowthRate)
		return true;
	return false;
}

// --- Build ---

void VrsSrsBuilder::Build(uint8_t* dst, uint32_t width, uint32_t height, uint32_t renderWidth, uint32_t renderHeight, const Params& params) const
{
	const RingRate* seq = nullptr;
	uint32_t seqLen = 0;
	GetRingRateSequence(params.srsPreset, seq, seqLen);

	const uint32_t eyeRenderWidth = renderWidth / 2u;
	const float ringStep = std::max(0.01f, params.ringGrowthRate);

	// Pre-fill: kCull for tiles outside renderRes (never shaded during world pass).
	// VRS is disabled before UI/Post passes via the FinishAccumulatingDispatch hook,
	// so the cull pre-fill only applies during the world render pass.
	std::memset(dst, SrsLevel::kCull, static_cast<size_t>(width) * height);

	// Per-eye: compute concentric elliptical zones from subrect UV.
	const EyeSubrectUV* eyeUVs[2] = { &params.leftSubrectUV, &params.rightSubrectUV };
	for (uint32_t eye = 0; eye < 2; ++eye) {
		const uint32_t eyeOffsetX = eye * eyeRenderWidth;
		const auto& uv = *eyeUVs[eye];

		// Ellipse center in tile coordinates.
		const float cx = static_cast<float>(eyeOffsetX) + (uv.x + uv.w * 0.5f) * static_cast<float>(eyeRenderWidth);
		const float cy = (uv.y + uv.h * 0.5f) * static_cast<float>(renderHeight);

		// Subrect half-dimensions in tiles (for directional angle detection).
		const float halfW = uv.w * 0.5f * static_cast<float>(eyeRenderWidth);
		const float halfH = uv.h * 0.5f * static_cast<float>(renderHeight);

		// Semi-axes of the circumscribed ellipse (sqrt2 * subrect half-dims).
		constexpr float kSqrt2 = 1.41421356f;
		const float a = halfW * kSqrt2;
		const float b = halfH * kSqrt2;

		if (a < 0.5f || b < 0.5f)
			continue;

		const uint32_t eyeMaxX = std::min(eyeOffsetX + eyeRenderWidth, renderWidth);
		for (uint32_t ty = 0; ty < renderHeight; ++ty) {
			for (uint32_t tx = eyeOffsetX; tx < eyeMaxX; ++tx) {
				const uint32_t idx = ty * width + tx;

				const float dxRaw = static_cast<float>(tx) + 0.5f - cx;
				const float dyRaw = static_cast<float>(ty) + 0.5f - cy;
				const float dxNorm = dxRaw / a;
				const float dyNorm = dyRaw / b;
				const float d = std::sqrt(dxNorm * dxNorm + dyNorm * dyNorm);

				// Determine ring index: ring 0 at d<=1.0, ring n at 1+n*step.
				uint32_t ringIdx;
				if (d <= 1.0f) {
					ringIdx = 0;
				} else {
					const float outer = (d - 1.0f) / ringStep;
					ringIdx = 1u + static_cast<uint32_t>(outer);

					// Boundary dithering: tiles just past a ring boundary
					// adopt the inner ring's rate in a checkerboard pattern,
					// expanding the better-quality zone outward.
					// Never dither the cull boundary — hard cull outside outer ring.
					if (params.enableBoundaryDither && ringIdx < seqLen - 1) {
						const float frac = outer - std::floor(outer);
						if (frac < kDitherFraction && ((tx + ty) & 1u)) {
							ringIdx--;
						}
					}
				}

				// Clamp to sequence bounds.
				if (ringIdx >= seqLen)
					ringIdx = seqLen - 1;

				dst[idx] = ResolveRingLevel(
					seq[ringIdx], dxRaw, dyRaw, halfW, halfH,
					params.enableDirectionalRates);
			}
		}
	}
}

void VrsSrsBuilder::UpdateCache(uint32_t width, uint32_t height, uint32_t renderWidth, uint32_t renderHeight, const Params& params)
{
	lastWidth = width;
	lastHeight = height;
	lastRenderWidth = renderWidth;
	lastRenderHeight = renderHeight;
	lastParams = params;
	hasCache = true;
}
