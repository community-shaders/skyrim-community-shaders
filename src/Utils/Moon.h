// Shared moon processing utilities
#pragma once

namespace Util::Moon
{
	/** @brief Intensity factor for a new (invisible) moon. */
	static constexpr float NewMoonIntensityFactor = 0.05f;
	/** @brief Intensity factor for crescent moon phases. */
	static constexpr float CrescentMoonIntensityFactor = 0.25f;
	/** @brief Intensity factor for a full moon. */
	static constexpr float FullMoonIntensityFactor = 1.0f;

	/** @brief Base colour of Masser (the larger, reddish moon). */
	static constexpr float4 MasserBaseColor = { 142.0f / 255.0f, 96.0f / 255.0f, 90.0f / 255.0f, 1.0f };
	/** @brief Base colour of Secunda (the smaller, greyish moon). */
	static constexpr float4 SecundaBaseColor = { 117.0f / 255.0f, 115.0f / 255.0f, 109.0f / 255.0f, 1.0f };

	/** @brief Lookup table mapping texture name substrings to moon phase enums. */
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
	 * @brief Compute a brightness intensity factor for the given moon phase.
	 *
	 * Linearly interpolates between crescent and full intensity based on how far
	 * the phase is from new moon.
	 *
	 * @param phase The moon phase to evaluate.
	 * @param newMoon Intensity returned for a new moon.
	 * @param crescent Intensity for the nearest crescent phase.
	 * @param full Intensity for a full moon.
	 * @return The interpolated intensity factor.
	 */
	inline float GetPhaseIntensityFactor(RE::Moon::Phases::Phase phase, float newMoon = NewMoonIntensityFactor, float crescent = CrescentMoonIntensityFactor, float full = FullMoonIntensityFactor)
	{
		if (phase == RE::Moon::Phases::Phase::kNewMoon) {
			return newMoon;
		} else {
			const float t = (abs(static_cast<float>(phase) - static_cast<float>(RE::Moon::Phases::Phase::kNewMoon)) - 1.0f) / 3.0f;
			return std::lerp(crescent, full, t);
		}
	}

	/**
	 * @brief Determine the moon phase from a texture filename.
	 *
	 * Searches the texture name (case-insensitive) for phase substrings
	 * such as "full", "new", "half_wan", etc.
	 *
	 * @param textureName The texture filename to parse.
	 * @return The detected moon phase, defaulting to kFull if unrecognised or null.
	 */
	inline RE::Moon::Phases::Phase GetPhaseFromTexture(const char* textureName)
	{
		if (!textureName)
			return RE::Moon::Phases::Phase::kFull;

		const size_t len = std::strlen(textureName);
		std::string lower;
		lower.reserve(len);
		for (size_t i = 0; i < len; ++i) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(textureName[i]))));
		}

		for (auto& [suffix, id] : PhaseLookup) {
			if (lower.find(suffix) != std::string::npos) {
				return id;
			}
		}

		return RE::Moon::Phases::Phase::kFull;
	}

	/**
	 * @brief Get the normalised world-space direction vector towards a moon.
	 * @param moon The moon object to query.
	 * @param applyMoonAndStarsCompat When true, applies axis swapping for Moon and Stars mod compatibility.
	 * @return The unit direction vector, or straight up (0,0,1) if the moon is invalid.
	 */
	inline RE::NiPoint3 GetDirection(const RE::Moon* moon, bool applyMoonAndStarsCompat = false)
	{
		if (!moon || !moon->root)
			return { 0.0f, 0.0f, 1.0f };

		auto dir = moon->root->world.rotate.GetVectorY();
		dir.Unitize();

		if (applyMoonAndStarsCompat) {
			std::swap(dir.x, dir.y);
			dir.x = -dir.x;
		}

		return dir;
	}

	/**
	 * @brief Compute the final blended colour contribution of a moon.
	 *
	 * Combines the moon's shader blend colour, base colour, phase intensity,
	 * and alpha to produce a premultiplied RGBA colour.
	 *
	 * @param moon The moon object to evaluate.
	 * @param baseColor The reference base colour for this moon (e.g. MasserBaseColor).
	 * @param newMoon Intensity factor for the new moon phase.
	 * @param crescent Intensity factor for crescent phases.
	 * @param full Intensity factor for the full moon phase.
	 * @return The premultiplied RGBA blend colour, or zero if the moon is invalid.
	 */
	inline float4 GetBlendColor(const RE::Moon* moon, const float4& baseColor, float newMoon = NewMoonIntensityFactor, float crescent = CrescentMoonIntensityFactor, float full = FullMoonIntensityFactor)
	{
		if (!moon || !moon->moonMesh)
			return {};

		const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get());
		if (!prop)
			return {};

		float phase = 1.0f;
		if (auto tex = prop->GetBaseTexture())
			phase = GetPhaseIntensityFactor(GetPhaseFromTexture(tex->name.c_str()), newMoon, crescent, full);

		float alpha = prop->kBlendColor.alpha;
		return { prop->kBlendColor.red * baseColor.x * phase * alpha, prop->kBlendColor.green * baseColor.y * phase * alpha, prop->kBlendColor.blue * baseColor.z * phase * alpha, alpha };
	}

}
