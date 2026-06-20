#pragma once

#include <string>
#include <variant>
#include <vector>

namespace FeatureConstraints
{
	/**
	 * @brief Identifies a specific setting that can be constrained
	 */
	struct SettingId
	{
		std::string featureShortName;
		std::string settingPath;       // e.g., "EnableDepthBufferCullingExterior"

		/** @brief Value equality by feature short name and setting path. */
		bool operator==(const SettingId& other) const
		{
			return featureShortName == other.featureShortName && settingPath == other.settingPath;
		}
	};

	/**
	 * @brief A constraint that one feature places on another feature's setting
	 */
	struct Constraint
	{
		SettingId targetSetting;                     // Which setting is affected
		std::variant<bool, int, float> forcedValue;  // Value to force
		std::string reason;                          // UI tooltip explanation
		bool recommendDisableAtBoot = false;         // Suggest disabling the source feature entirely
	};

	/**
	 * @brief Result of checking constraints on a setting
	 */
	struct ConstraintResult
	{
		bool isConstrained = false;
		std::variant<bool, int, float> forcedValue;

		struct Source
		{
			std::string featureName;       // Display name (e.g. "Terrain Blending")
			std::string featureShortName;  // Menu navigation key (e.g. "TerrainBlending")
			std::string reason;
			bool recommendDisableAtBoot;
		};
		std::vector<Source> sources;

		/**
		 * @brief Check if any source recommends disabling at boot
		 */
		bool AnyRecommendDisableAtBoot() const
		{
			for (const auto& src : sources) {
				if (src.recommendDisableAtBoot)
					return true;
			}
			return false;
		}
	};

	/**
	 * @brief Query if a setting is constrained by any active feature
	 * @param setting The setting to check
	 * @return ConstraintResult with all sources causing the constraint
	 */
	ConstraintResult GetConstraints(const SettingId& setting);

	/**
	 * @brief Get all active constraints across all features
	 * @return Vector of setting IDs and their constraint results
	 */
	std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints();

	/**
	 * @brief Build a formatted tooltip string for a constrained setting
	 * @param result The constraint result to format
	 * @return Formatted string suitable for ImGui tooltip
	 */
	std::string BuildConstraintTooltip(const ConstraintResult& result);

	/**
	 * @brief Format a constraint value as a string for display
	 * @param value The variant value to format
	 * @return String representation of the value
	 */
	std::string FormatConstraintValue(const std::variant<bool, int, float>& value);
}
