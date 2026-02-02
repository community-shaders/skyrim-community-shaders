#include "FeatureConstraints.h"
#include "Feature.h"

#include <unordered_set>

namespace FeatureConstraints
{
	ConstraintResult GetConstraints(const SettingId& setting)
	{
		ConstraintResult result;

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded)
				continue;

			auto constraints = feature->GetActiveConstraints();
			for (const auto& constraint : constraints) {
				if (constraint.targetSetting == setting) {
					if (!result.isConstrained) {
						result.isConstrained = true;
						result.forcedValue = constraint.forcedValue;
					}
					result.sources.push_back({ feature->GetName(),
						constraint.reason,
						constraint.recommendDisableAtBoot });
				}
			}
		}

		return result;
	}

	std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints()
	{
		std::vector<std::pair<SettingId, ConstraintResult>> allConstraints;
		std::unordered_set<std::string> processedKeys;  // featureShortName|settingPath for O(1) lookup

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded)
				continue;

			auto constraints = feature->GetActiveConstraints();
			for (const auto& constraint : constraints) {
				std::string key = constraint.targetSetting.featureShortName + "|" + constraint.targetSetting.settingPath;
				if (processedKeys.insert(key).second) {
					auto result = GetConstraints(constraint.targetSetting);
					if (result.isConstrained) {
						allConstraints.push_back({ constraint.targetSetting, result });
					}
				}
			}
		}

		return allConstraints;
	}

	/**
	 * @brief Get constraints that would be created by enabling a specific feature
	 * @param featureToEnable The feature that would be enabled
	 * @return Vector of setting IDs and constraint results that would be created
	 */
	std::vector<std::pair<SettingId, ConstraintResult>> GetConstraintsFromEnablingFeature(Feature* featureToEnable)
	{
		std::vector<std::pair<SettingId, ConstraintResult>> newConstraints;
		std::unordered_set<std::string> processedKeys;

		// Get constraints from the feature we're enabling
		auto constraints = featureToEnable->GetActiveConstraints();
		for (const auto& constraint : constraints) {
			std::string key = constraint.targetSetting.featureShortName + "|" + constraint.targetSetting.settingPath;
			if (processedKeys.insert(key).second) {
				// Check if this setting is already constrained by other features
				auto existingResult = GetConstraints(constraint.targetSetting);
				if (!existingResult.isConstrained) {
					// This constraint would be new
					ConstraintResult newResult;
					newResult.isConstrained = true;
					newResult.forcedValue = constraint.forcedValue;
					newResult.sources.push_back({ featureToEnable->GetName(),
						constraint.reason,
						constraint.recommendDisableAtBoot });
					newConstraints.push_back({ constraint.targetSetting, newResult });
				}
			}
		}

		return newConstraints;
	}

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
		const std::function<void()>& revertChange)
	{
		std::vector<std::pair<SettingId, ConstraintResult>> newConstraints;
		std::unordered_set<std::string> processedKeys;

		// Get current constraints from this feature
		auto currentConstraints = feature->GetActiveConstraints();
		std::unordered_set<std::string> currentKeys;
		for (const auto& constraint : currentConstraints) {
			currentKeys.insert(constraint.targetSetting.featureShortName + "|" + constraint.targetSetting.settingPath);
		}

		// Apply the setting change temporarily
		applyChange();

		// Get new constraints after the change
		auto newConstraintsFromFeature = feature->GetActiveConstraints();

		// Revert the change
		revertChange();

		// Find constraints that would be newly created
		for (const auto& constraint : newConstraintsFromFeature) {
			std::string key = constraint.targetSetting.featureShortName + "|" + constraint.targetSetting.settingPath;
			if (processedKeys.insert(key).second && currentKeys.find(key) == currentKeys.end()) {
				// This constraint would be new - check if the setting is already constrained by other features
				auto existingResult = GetConstraints(constraint.targetSetting);
				if (!existingResult.isConstrained) {
					// This constraint would be newly created
					ConstraintResult newResult;
					newResult.isConstrained = true;
					newResult.forcedValue = constraint.forcedValue;
					newResult.sources.push_back({ feature->GetName(),
						constraint.reason,
						constraint.recommendDisableAtBoot });
					newConstraints.push_back({ constraint.targetSetting, newResult });
				}
			}
		}

		return newConstraints;
	}

	std::string BuildConstraintTooltip(const ConstraintResult& result)
	{
		if (!result.isConstrained || result.sources.empty())
			return "";

		std::string tooltip = "This setting is constrained by:\n";
		for (const auto& src : result.sources) {
			tooltip += "\n- " + src.featureName + ":\n  " + src.reason;
			if (src.recommendDisableAtBoot) {
				tooltip += "\n  (Consider disabling this feature at boot for best compatibility)";
			}
		}

		tooltip += "\n\nForced value: " + FormatConstraintValue(result.forcedValue);

		return tooltip;
	}

	std::string FormatConstraintValue(const std::variant<bool, int, float>& value)
	{
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value) ? "Enabled" : "Disabled";
		} else if (std::holds_alternative<int>(value)) {
			return std::to_string(std::get<int>(value));
		} else if (std::holds_alternative<float>(value)) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%.2f", std::get<float>(value));
			return buf;
		}
		return "Unknown";
	}
}
