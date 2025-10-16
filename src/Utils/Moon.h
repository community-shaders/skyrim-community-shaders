// Shared moon processing utilities
#pragma once

#include "RE/M/Moon.h"

namespace Util::Moon
{
	// Moon phase intensity constants
	static constexpr float SecundaIntensityFactor = 0.67f;
	static constexpr float NewMoonIntensityFactor = 0.05f;
	static constexpr float CrescentMoonIntensityFactor = 0.25f;
	static constexpr float FullMoonIntensityFactor = 1.0f;

	// Moon base colors (RGB/255)
	static constexpr float4 MasserBaseColor = { 142.0f / 255.0f, 96.0f / 255.0f, 90.0f / 255.0f, 1.0f };
	static constexpr float4 SecundaBaseColor = { 117.0f / 255.0f, 115.0f / 255.0f, 109.0f / 255.0f, 1.0f };

	// Phase lookup table for determining moon phase from texture name
	static constexpr std::array<std::pair<std::string_view, RE::Moon::Phases::Phase>, 8> PhaseLookup{
		{ { "full", RE::Moon::Phases::Phase::kFull },
			{ "three_wan", RE::Moon::Phases::Phase::kWaningGibbous },
			{ "half_wan", RE::Moon::Phases::Phase::kWaningQuarter },
			{ "one_wan", RE::Moon::Phases::Phase::kWaningCrescent },
			{ "new", RE::Moon::Phases::Phase::kNewMoon },
			{ "one_wax", RE::Moon::Phases::Phase::kWaxingCrescent },
			{ "half_wax", RE::Moon::Phases::Phase::kWaxingQuarter },
			{ "three_wax", RE::Moon::Phases::Phase::kWaxingGibbous } }
	};

	/**
	 * @brief Get the phase-based intensity factor for a moon
	 * @param phase The current phase of the moon
	 * @return Intensity factor between 0.05 (new moon) and 1.0 (full moon)
	 */
	inline float GetPhaseIntensityFactor(RE::Moon::Phases::Phase phase)
	{
		if (phase == RE::Moon::Phases::Phase::kNewMoon) {
			return NewMoonIntensityFactor;
		} else {
			const float t = (abs(static_cast<float>(phase) - static_cast<float>(RE::Moon::Phases::Phase::kNewMoon)) - 1.0f) / 3.0f;
			return std::lerp(CrescentMoonIntensityFactor, FullMoonIntensityFactor, t);
		}
	}

	/**
	 * @brief Detect moon phase from texture name
	 * @param textureName Name of the moon texture
	 * @return Detected phase (defaults to kFull if not found)
	 */
	inline RE::Moon::Phases::Phase GetPhaseFromTexture(const char* textureName)
	{
		if (!textureName)
			return RE::Moon::Phases::Phase::kFull;

		// Convert texture name to lowercase for phase detection
		const size_t len = std::strlen(textureName);
		std::string lower;
		lower.reserve(len);
		for (size_t i = 0; i < len; ++i) {
			lower.push_back(static_cast<char>(std::tolower(textureName[i])));
		}

		// Search for phase identifier in texture name
		for (auto& [suffix, id] : PhaseLookup) {
			if (lower.find(suffix) != std::string::npos) {
				return id;
			}
		}

		return RE::Moon::Phases::Phase::kFull;
	}

	/**
	 * @brief Get moon direction from its rotation matrix
	 * @param moon Pointer to the moon object
	 * @param applyMoonAndStarsCompat Apply Moon and Stars mod compatibility adjustment
	 * @return Normalized direction vector (Y-axis of rotation matrix)
	 */
	inline RE::NiPoint3 GetDirection(const RE::Moon* moon, bool applyMoonAndStarsCompat = false)
	{
		if (!moon || !moon->root)
			return { 0.0f, 0.0f, 1.0f };

		auto dir = moon->root->local.rotate.GetVectorY();
		dir.Unitize();

		// Moon and Stars adjusts some intermediary rotation matrices for the moon
		// Directly changing the directions here avoids 3 matrix multiplications and a vector rotation
		if (applyMoonAndStarsCompat) {
			std::swap(dir.x, dir.y);
			dir.x = -dir.x;
		}

		return dir;
	}

	/**
	 * @brief Calculate moon color with phase-based intensity
	 * @param moon Pointer to the moon object
	 * @param moonGlareColor Base moon glare color from weather
	 * @param baseColor Moon-specific base color tint (Masser or Secunda)
	 * @param intensityScale Additional intensity scaling factor (e.g., Secunda is dimmer)
	 * @return Calculated moon color with all factors applied
	 */
	inline float4 CalculateColor(const RE::Moon* moon, const float4& moonGlareColor, const float4& baseColor, float intensityScale = 1.0f)
	{
		// Start with moon glare color and apply base color tint
		float4 color = moonGlareColor * baseColor;

		// Apply phase-based intensity if moon mesh exists
		if (moon && moon->moonMesh && moon->moonMesh.get()) {
			if (const auto moonShaderProperty = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().properties[1].get())) {
				if (auto texture = moonShaderProperty->GetBaseTexture()) {
					const auto phase = GetPhaseFromTexture(texture->name.c_str());
					color *= GetPhaseIntensityFactor(phase);
				}
			}
		}

		// Apply moon-specific intensity scaling (e.g., Secunda is dimmer than Masser)
		color *= intensityScale;

		return color;
	}

	/**
	 * @brief Process moon to get both direction and color
	 * @param moon Pointer to the moon object
	 * @param moonGlareColor Base moon glare color from weather
	 * @param baseColor Moon-specific base color tint (Masser or Secunda)
	 * @param intensityScale Additional intensity scaling factor
	 * @param applyMoonAndStarsCompat Apply Moon and Stars mod compatibility adjustment
	 * @return Pair of (direction as float4, color as float4)
	 */
	inline std::pair<float4, float4> ProcessMoon(const RE::Moon* moon, const float4& moonGlareColor, const float4& baseColor, float intensityScale = 1.0f, bool applyMoonAndStarsCompat = false)
	{
		if (!moon) {
			return { { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
		}

		const auto dir = GetDirection(moon, applyMoonAndStarsCompat);
		const float4 direction = { dir.x, dir.y, dir.z, 0.0f };
		const float4 color = CalculateColor(moon, moonGlareColor, baseColor, intensityScale);

		return { direction, color };
	}
}
