#include "SceneHandoff.h"

#include <array>
#include <nlohmann/json.hpp>

#include "Features/Effects11.h"
#include "Globals.h"

namespace E11Handoff
{
	namespace
	{
		constexpr std::array<std::string_view, static_cast<size_t>(Feature::Count)> kShortNames{
			"ImageBasedLighting",
			"CloudShadows",
			"ExponentialHeightFog"
		};

		/** @brief An Effects 11 setting a feature replaces; an empty key covers its whole category. */
		struct E11Setting
		{
			Feature feature;
			std::string_view category;
			std::string_view key;
		};

		constexpr std::array kE11Settings{
			E11Setting{ Feature::IBL, "IMAGEBASEDLIGHTING", {} },
			E11Setting{ Feature::IBL, "EFFECT", "EnableImageBasedLighting" },
			E11Setting{ Feature::CloudShadows, "CLOUDSHADOWS", {} },
			E11Setting{ Feature::CloudShadows, "EFFECT", "EnableCloudShadows" },
			E11Setting{ Feature::HeightFog, "ENVIRONMENT", "FogColorMultiplier" },
			E11Setting{ Feature::HeightFog, "ENVIRONMENT", "FogColorCurve" },
			E11Setting{ Feature::HeightFog, "ENVIRONMENT", "FogAmountMultiplier" },
			E11Setting{ Feature::HeightFog, "ENVIRONMENT", "FogCurveMultiplier" },
			E11Setting{ Feature::HeightFog, "ENVIRONMENT", "FogColorFilter" },
			E11Setting{ Feature::HeightFog, "ENVIRONMENT", "FogColorFilterAmount" },
		};
	}

	std::string_view GetShortName(Feature feature)
	{
		assert(feature < Feature::Count);
		return kShortNames[static_cast<size_t>(feature)];
	}

	std::optional<Feature> FromShortName(std::string_view shortName)
	{
		for (size_t i = 0; i < kShortNames.size(); ++i) {
			if (kShortNames[i] == shortName)
				return static_cast<Feature>(i);
		}
		return std::nullopt;
	}

	bool IsE11SettingHandedOff(std::string_view category, std::string_view key)
	{
		const auto& effects11 = globals::features::effects11;
		return std::ranges::any_of(kE11Settings, [&](const E11Setting& setting) {
			return setting.category == category && (setting.key.empty() || setting.key == key) && effects11.IsHandedOff(setting.feature);
		});
	}

	nlohmann::json ToManifest()
	{
		auto manifest = nlohmann::json::object();
		for (size_t i = 0; i < kShortNames.size(); ++i) {
			if (globals::features::effects11.IsHandedOff(static_cast<Feature>(i)))
				manifest[std::string(kShortNames[i])] = true;
		}
		return manifest;
	}

	void ApplyManifest(const nlohmann::json& sceneControl)
	{
		for (size_t i = 0; i < kShortNames.size(); ++i) {
			bool handedOff = false;
			if (sceneControl.is_object()) {
				const auto entry = sceneControl.find(std::string(kShortNames[i]));
				handedOff = entry != sceneControl.end() && entry->is_boolean() && entry->get<bool>();
			}
			globals::features::effects11.SetHandedOff(static_cast<Feature>(i), handedOff);
		}
	}
}
