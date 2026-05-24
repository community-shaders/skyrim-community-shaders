// Shared moon processing utilities
#pragma once

namespace Util::Moon
{
	// Moon phase intensity constants
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

	inline float GetPhaseIntensityFactor(RE::Moon::Phases::Phase phase, float newMoon = NewMoonIntensityFactor, float crescent = CrescentMoonIntensityFactor, float full = FullMoonIntensityFactor)
	{
		if (phase == RE::Moon::Phases::Phase::kNewMoon) {
			return newMoon;
		} else {
			const float t = (abs(static_cast<float>(phase) - static_cast<float>(RE::Moon::Phases::Phase::kNewMoon)) - 1.0f) / 3.0f;
			return std::lerp(crescent, full, t);
		}
	}

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
