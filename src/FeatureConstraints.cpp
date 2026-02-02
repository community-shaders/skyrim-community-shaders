#include "FeatureConstraints.h"
#include "Feature.h"

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
		std::vector<SettingId> processedSettings;

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded)
				continue;

			auto constraints = feature->GetActiveConstraints();
			for (const auto& constraint : constraints) {
				// Check if we've already processed this setting
				bool found = false;
				for (const auto& processed : processedSettings) {
					if (processed == constraint.targetSetting) {
						found = true;
						break;
					}
				}

				if (!found) {
					processedSettings.push_back(constraint.targetSetting);
					auto result = GetConstraints(constraint.targetSetting);
					if (result.isConstrained) {
						allConstraints.push_back({ constraint.targetSetting, result });
					}
				}
			}
		}

		return allConstraints;
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

	std::vector<Constraint> GetPotentialConstraints(const std::string& featureShortName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->GetShortName() == featureShortName) {
				return feature->GetActiveConstraints();
			}
		}
		return {};
	}

	bool WouldCauseConflicts(const std::string& featureShortName, std::vector<std::pair<Constraint, bool>>& outConflicts)
	{
		outConflicts.clear();

		// Get potential constraints from the feature
		auto potentialConstraints = GetPotentialConstraints(featureShortName);
		if (potentialConstraints.empty()) {
			return false;
		}

		// For each constraint, check if the target setting is currently in conflict
		for (const auto& constraint : potentialConstraints) {
			// A conflict exists if:
			// 1. The constraint would force a setting to a value
			// 2. The setting is currently set to a different (conflicting) value
			// For bool constraints forcing to false, conflict if currently true
			if (std::holds_alternative<bool>(constraint.forcedValue)) {
				bool forcedValue = std::get<bool>(constraint.forcedValue);
				// We can't easily check the current value of arbitrary settings here
				// without more infrastructure, so we just report all constraints
				// The UI can then check if the specific setting is currently different
				outConflicts.push_back({ constraint, !forcedValue });
			}
		}

		return !outConflicts.empty();
	}
}
