#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string_view>

/** @brief CS features Effects 11 replaces, which an E11 preset can hand back through its manifest's sceneControl object. */
namespace E11Handoff
{
	enum class Feature : uint8_t
	{
		IBL,
		CloudShadows,
		HeightFog,
		Count
	};

	/** @brief The feature's CS short name, which is also its sceneControl manifest key. */
	std::string_view GetShortName(Feature feature);
	/** @brief The feature with this CS short name, or null when Effects 11 does not replace it. */
	std::optional<Feature> FromShortName(std::string_view shortName);

	/** @brief Whether an Effects 11 setting belongs to a handed-off feature; an empty key asks about the whole category. */
	bool IsE11SettingHandedOff(std::string_view category, std::string_view key = {});

	/** @brief The handed-off features as a sceneControl manifest object; empty when none are. */
	nlohmann::json ToManifest();
	/** @brief Sets every handoff flag from a sceneControl manifest object; features without a true entry stay with Effects 11. */
	void ApplyManifest(const nlohmann::json& sceneControl);
}
