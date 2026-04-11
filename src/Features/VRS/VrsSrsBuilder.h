#pragma once

#include <cstdint>

/// Shading rate level constants — indices into the NVAPI per-viewport LUT.
/// Higher value = coarser shading rate.  Stored per-tile in the SRS R8_UINT texture.
namespace SrsLevel
{
	constexpr uint8_t k1x1  = 0;   // 1x1: native rate
	constexpr uint8_t k2x1  = 1;   // 2x1: half rate, preserve horizontal resolution
	constexpr uint8_t k1x2  = 2;   // 1x2: half rate, preserve vertical resolution
	constexpr uint8_t k2x2  = 3;   // 2x2: quarter rate
	constexpr uint8_t k4x2  = 4;   // 4x2: eighth rate, preserve horizontal resolution
	constexpr uint8_t k2x4  = 5;   // 2x4: eighth rate, preserve vertical resolution
	constexpr uint8_t k4x4  = 6;   // 4x4: sixteenth rate
	constexpr uint8_t kCull = 7;   // CULL: skip pixel shading entirely
	constexpr uint8_t kCount = 8;
}

/// Ring rate category — determines how a ring is resolved to an SrsLevel.
/// Symmetric rates always produce the same level; directional rates choose
/// a horizontal or vertical variant based on the tile's angular position
/// relative to the subrect center diagonal.
enum class RingRate : uint8_t
{
	Rate_1x1,     // -> SrsLevel::k1x1
	Rate_Half,    // -> k2x1 (horizontal sector) or k1x2 (vertical sector)
	Rate_2x2,     // -> SrsLevel::k2x2
	Rate_Eighth,  // -> k4x2 (horizontal sector) or k2x4 (vertical sector)
	Rate_4x4,     // -> SrsLevel::k4x4
	Rate_Cull,    // -> SrsLevel::kCull
};

/// SRS pattern builder — purely CPU-side, generates per-tile shading rates.
/// Elliptical concentric rings with rates adapted to human peripheral acuity:
/// center = native, outer rings = progressively coarser, outside = cull.
/// Directional-adaptive rates (2×1/1×2, 4×2/2×4) align pixel coarsening
/// with the weaker perceptual axis at each angular position.
class VrsSrsBuilder
{
public:
	struct EyeSubrectUV
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 1.0f;
		float h = 1.0f;
	};

	struct Params
	{
		uint32_t srsPreset = 0;  ///< 0=Default (6-step), 1=Faster (4-step), 2=Extreme (3-step)
		EyeSubrectUV leftSubrectUV{};   ///< Foveal center UV for left eye
		EyeSubrectUV rightSubrectUV{};  ///< Foveal center UV for right eye
		float ringGrowthRate = 0.25f;    ///< Ring boundary step as fraction of base ellipse
		bool enableDirectionalRates = true;  ///< Asymmetric 2×1/1×2 based on tile angle
		bool enableBoundaryDither = true;    ///< Checkerboard dither at ring boundaries
	};

	bool NeedsRebuild(uint32_t width, uint32_t height, uint32_t renderWidth, uint32_t renderHeight, const Params& params) const;

	/// Fill dst with per-tile SrsLevel values for the full stereo surface.
	void Build(uint8_t* dst, uint32_t width, uint32_t height, uint32_t renderWidth, uint32_t renderHeight, const Params& params) const;

	void UpdateCache(uint32_t width, uint32_t height, uint32_t renderWidth, uint32_t renderHeight, const Params& params);

	/// Generate an R8G8B8A8 debug visualisation from SRS tile data.
	static void BuildDebugVisualization(uint32_t* dst, const uint8_t* src, uint32_t width, uint32_t height);

	/// Retrieve the ring rate sequence for a preset index.
	static void GetRingRateSequence(uint32_t preset, const RingRate*& out, uint32_t& count);

	/// Resolve a RingRate into a concrete SrsLevel.
	static uint8_t ResolveRingLevel(RingRate rate, float dx, float dy, float halfW, float halfH, bool directional);

private:
	uint32_t lastWidth = 0;
	uint32_t lastHeight = 0;
	uint32_t lastRenderWidth = 0;
	uint32_t lastRenderHeight = 0;
	bool hasCache = false;
	Params lastParams{};
};
