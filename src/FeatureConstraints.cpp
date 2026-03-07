#include "FeatureConstraints.h"
#include "Feature.h"
#include "Globals.h"
#include "State.h"

#include <unordered_set>

namespace FeatureConstraints
{
	ConstraintResult GetConstraints(const SettingId& setting)
	{
		if (globals::state && globals::state->IsDeveloperMode())
			return {};
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
					} else if (constraint.forcedValue != result.forcedValue) {
						// Two features disagree on the forced value; first one wins.
						// Log once so it surfaces during development / testing.
						logger::warn("[FeatureConstraints] Conflict on {}.{}: {} wants {}, but {} already forced {}",
							setting.featureShortName, setting.settingPath,
							feature->GetName(), FormatConstraintValue(constraint.forcedValue),
							result.sources[0].featureName, FormatConstraintValue(result.forcedValue));
					}
					result.sources.push_back({ feature->GetName(),
						feature->GetShortName(),
						constraint.reason,
						constraint.recommendDisableAtBoot });
				}
			}
		}

		return result;
	}

	std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints()
	{
		if (globals::state && globals::state->IsDeveloperMode())
			return {};
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
