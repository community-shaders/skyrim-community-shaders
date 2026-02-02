#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

struct Feature;

namespace FeatureConstraints
{
	/**
	 * @brief Identifies a specific setting that can be constrained
	 */
	struct SettingId
	{
		std::string featureShortName;  // e.g., "VR"
		std::string settingPath;       // e.g., "EnableDepthBufferCullingExterior"

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
			std::string featureName;
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
	 * @brief Get constraints that would be created by enabling a specific feature
	 * @param featureToEnable The feature that would be enabled
	 * @return Vector of setting IDs and constraint results that would be created
	 */
	std::vector<std::pair<SettingId, ConstraintResult>> GetConstraintsFromEnablingFeature(Feature* featureToEnable);

	/**
	 * @brief Get constraints that would be created by a setting change
	 * @param feature The feature whose setting is changing
	 * @param applyChange Function to apply the setting change temporarily
	 * @param revertChange Function to revert the setting change
	 * @return Vector of setting IDs and constraint results that would be created by the change
	 */
	std::vector<std::pair<SettingId, ConstraintResult>> GetConstraintsFromSettingChange(
		Feature* feature,
		const std::function<void()>& applyChange,
		const std::function<void()>& revertChange);

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
