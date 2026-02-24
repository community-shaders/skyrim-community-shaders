#include "SceneSettingsUI.h"

#include <cmath>
#include <map>

#include "../Globals.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"

namespace SceneSettingsUI
{
	using C = ThemeManager::Constants;

	// --- Feature name resolution by scene type ---

	static std::vector<std::string> GetFeatureNamesForType(SceneType type)
	{
		return (type == SceneType::InteriorOnly) ? SceneSettingsManager::GetInteriorRelevantFeatureNames() : SceneSettingsManager::GetExteriorRelevantFeatureNames();
	}

	// --- Duplicate checking by scene type ---

	static bool IsAlreadyAdded(SceneType type, const std::string& feature, const std::string& key, Period period)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		return (type == SceneType::TimeOfDay) ? manager->HasEntryForPeriod(feature, key, period, EntrySource::User) : manager->HasEntryFromSource(type, feature, key, EntrySource::User);
	}

	// --- Shared Drawing ---

	void DrawAddSettingUI(SceneType type, AddSettingState& state, Period period, const char* labelPrefix, bool addToAllPeriods)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

		ImGui::Spacing();

		// Optional inline label (e.g. period name) with fixed width for alignment
		if (labelPrefix) {
			ImGui::Text("%s", labelPrefix);
			ImGui::SameLine(C::Em(C::SCENE_TOD_LABEL_EM));
		}

		// Feature dropdown
		if (state.cachedFeatureNames.empty())
			state.cachedFeatureNames = GetFeatureNamesForType(type);

		auto displayName = (state.selectedFeatureIdx >= 0 &&
							   state.selectedFeatureIdx < static_cast<int>(state.cachedFeatureNames.size())) ?
		                       SceneSettingsManager::GetFeatureDisplayName(state.cachedFeatureNames[state.selectedFeatureIdx]) :
		                       std::string("Select Feature...");
		const char* featurePreview = displayName.c_str();

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * C::SCENE_FEATURE_DROPDOWN_RATIO);
		if (ImGui::BeginCombo("##FeatureSelect", featurePreview)) {
			for (int i = 0; i < static_cast<int>(state.cachedFeatureNames.size()); ++i) {
				auto itemLabel = SceneSettingsManager::GetFeatureDisplayName(state.cachedFeatureNames[i]);
				if (ImGui::Selectable(itemLabel.c_str(), i == state.selectedFeatureIdx)) {
					state.selectedFeatureIdx = i;
					state.cachedSettingKeys = SceneSettingsManager::GetFeatureSettingKeys(state.cachedFeatureNames[i]);
				}
				if (i == state.selectedFeatureIdx)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();

		// Setting dropdown — selecting an entry auto-adds it
		{
			auto _ = Util::DisableGuard(state.selectedFeatureIdx < 0);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			if (ImGui::BeginCombo("##SettingSelect", "Select Setting...")) {
				for (int i = 0; i < static_cast<int>(state.cachedSettingKeys.size()); ++i) {
					auto& featureName = state.cachedFeatureNames[state.selectedFeatureIdx];
					auto& key = state.cachedSettingKeys[i];

					// Check if already added (for all-periods mode, disabled only when present in every period)
					bool alreadyAdded = false;
					if (state.selectedFeatureIdx >= 0) {
						if (addToAllPeriods) {
							alreadyAdded = true;
							for (int p = 0; p < kPeriodCount && alreadyAdded; ++p)
								alreadyAdded = IsAlreadyAdded(type, featureName, key, static_cast<Period>(p));
						} else {
							alreadyAdded = IsAlreadyAdded(type, featureName, key, period);
						}
					}

					if (alreadyAdded) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
						ImGui::Selectable(key.c_str(), false, ImGuiSelectableFlags_Disabled);
						ImGui::PopStyleColor();
					} else if (ImGui::Selectable(key.c_str(), false)) {
						auto currentValue = SceneSettingsManager::GetFeatureSettingValue(featureName, key);
						if (addToAllPeriods) {
							for (int p = 0; p < kPeriodCount; ++p)
								if (!IsAlreadyAdded(type, featureName, key, static_cast<Period>(p)))
									manager->AddSetting(type, featureName, key, currentValue, static_cast<Period>(p));
						} else {
							manager->AddSetting(type, featureName, key, currentValue, period);
						}
					}
				}
				ImGui::EndCombo();
			}
		}
	}

	void DrawValueEditor(SceneType type, size_t index, float inputWidth)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entry = manager->GetEntries(type)[index];
		auto settingType = SceneSettingsManager::DetectSettingType(entry.value);

		switch (settingType) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = entry.value.is_boolean() ? entry.value.get<bool>() : (entry.value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val))
					manager->UpdateEntryValue(type, index, entry.value.is_boolean() ? json(val) : json(val ? 1 : 0));
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = entry.value.is_number() ? entry.value.get<float>() : 0.0f;
				if (!std::isfinite(val))
					val = 0.0f;
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputFloat("##val", &val, 0.0f, 0.0f, "%.3f"))
					if (std::isfinite(val))
						manager->UpdateEntryValue(type, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(type);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputInt("##val", &val, 0, 0))
					manager->UpdateEntryValue(type, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(type);
			}
			break;
		default:
			ImGui::TextDisabled("(unsupported type)");
			break;
		}
	}

	bool DrawSettingEntry(SceneType type, size_t index, PopupState& popups)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(type);
		if (index >= entries.size())
			return false;

		const auto& entry = entries[index];

		ImGui::PushID(static_cast<int>(index));

		// Setting key label (no feature prefix — grouped by feature already)
		float availWidth = ImGui::GetContentRegionAvail().x;
		ImGui::Text("%s", entry.settingKey.c_str());

		// Value editor (right-aligned)
		ImGui::SameLine(availWidth * C::SCENE_VALUE_LABEL_OFFSET_RATIO);

		bool isOverwrite = entry.source == EntrySource::Overwrite;

		bool readOnly = isOverwrite ||
		                (type != SceneType::TimeOfDay &&
							manager->HasActiveOverwrite(type, entry.featureShortName, entry.settingKey));

		if (readOnly)
			ImGui::BeginDisabled();

		DrawValueEditor(type, index, C::Em(C::SCENE_VALUE_INPUT_EM));

		if (readOnly)
			ImGui::EndDisabled();

		// Active/Pause toggle
		ImGui::SameLine();
		bool active = !entry.paused;
		if (Util::FeatureToggle("##active", &active))
			manager->TogglePauseEntry(type, index);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(entry.paused ? "Paused - click to resume" : "Active - click to pause");

		// Delete button
		ImGui::SameLine();
		{
			auto styledButton = Util::ErrorButtonStyle();
			if (ImGui::Button("X", ImVec2(C::Em(C::SCENE_DELETE_BUTTON_EM), 0))) {
				if (isOverwrite) {
					popups.pendingDeleteIndex = index;
					popups.deleteSingleOverwrite.message = std::format(
						"Delete overwrite file '{}'?\nThis will permanently remove the file from disk.",
						entry.sourceFilename);
					popups.deleteSingleOverwrite.Request();
				} else {
					manager->RemoveSetting(type, index);
					ImGui::PopID();
					return true;
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(isOverwrite ? "Delete overwrite file from disk" : "Remove this setting");

		ImGui::PopID();
		return false;
	}

	void DrawPopups(SceneType type, PopupState& popups)
	{
		auto* manager = SceneSettingsManager::GetSingleton();

		if (popups.deleteAllOverwrites.Draw())
			manager->DeleteAllOverwrites(type);

		if (popups.deleteSingleOverwrite.Draw()) {
			if (popups.pendingDeleteIndex < manager->GetEntries(type).size())
				manager->RemoveSetting(type, popups.pendingDeleteIndex);
			popups.pendingDeleteIndex = SIZE_MAX;
		}

		if (popups.deleteRowOverwrite.Draw()) {
			// Delete in reverse index order so earlier indices remain valid
			std::sort(popups.pendingDeleteRow.begin(), popups.pendingDeleteRow.end(), std::greater<>());
			for (auto idx : popups.pendingDeleteRow)
				if (idx < manager->GetEntries(type).size())
					manager->RemoveSetting(type, idx);
			popups.pendingDeleteRow.clear();
		}

		if (popups.deleteAllUser.Draw())
			manager->DeleteAllUserSettings(type);
	}

	/// Draw entries grouped by feature with collapsible tree nodes.
	static void DrawGroupedEntries(SceneType type, PopupState& popups,
		const std::vector<size_t>& indices)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(type);

		std::map<std::string, std::vector<size_t>> grouped;
		for (auto i : indices)
			if (i < entries.size())
				grouped[entries[i].featureShortName].push_back(i);

		// Sort settings within each feature group alphabetically by key
		for (auto& [_, featureIndices] : grouped)
			std::sort(featureIndices.begin(), featureIndices.end(), [&entries](size_t a, size_t b) {
				return entries[a].settingKey < entries[b].settingKey;
			});

		for (const auto& [featureName, featureIndices] : grouped) {
			auto label = SceneSettingsManager::GetFeatureDisplayName(featureName) + ":";
			if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
				for (auto i : featureIndices)
					if (i < entries.size())
						DrawSettingEntry(type, i, popups);
				ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));
				ImGui::TreePop();
			}
		}
	}

	void DrawSectionHeader(const char* label, const ImVec4& color, const char* idSuffix,
		bool allPaused, std::function<void()> onTogglePause, std::function<void()> onDeleteAll)
	{
		ImGui::Spacing();
		ImGui::TextColored(color, "%s", label);
		ImGui::SameLine();
		auto pauseLabel = std::format("{}{}", allPaused ? "Unpause All" : "Pause All", idSuffix);
		if (ImGui::SmallButton(pauseLabel.c_str()))
			onTogglePause();
		ImGui::SameLine();
		auto deleteLabel = std::format("Delete All{}", idSuffix);
		if (ImGui::SmallButton(deleteLabel.c_str()))
			onDeleteAll();
		ImGui::Separator();
	}

	void DrawEntrySections(SceneType type, PopupState& popups)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(type);
		auto& theme = globals::menu->GetSettings().Theme;

		// Split indices by source
		std::vector<size_t> overwriteIndices, userIndices;
		for (size_t i = 0; i < entries.size(); ++i)
			(entries[i].source == EntrySource::Overwrite ? overwriteIndices : userIndices).push_back(i);

		if (!overwriteIndices.empty()) {
			DrawSectionHeader("Overwrite Files", theme.StatusPalette.InfoColor, "##ow", manager->AreAllOverwritesPaused(type), [&] { manager->SetAllOverwritesPaused(type, !manager->AreAllOverwritesPaused(type)); }, [&] { popups.deleteAllOverwrites.Request(); });
			DrawGroupedEntries(type, popups, overwriteIndices);
		}

		if (!userIndices.empty()) {
			DrawSectionHeader("User Settings", theme.FeatureHeading.ColorDefault, "##usr", manager->AreAllUserPaused(type), [&] { manager->SetAllUserPaused(type, !manager->AreAllUserPaused(type)); }, [&] { popups.deleteAllUser.Request(); });
			DrawGroupedEntries(type, popups, userIndices);
		}
	}

	bool DrawCategoryPanel(const char* category, const std::string& selected, void (*drawFn)())
	{
		if (selected != category)
			return false;
		// Wrap in a scrollable child since the parent disables scrolling (kStickyHeaderFlags)
		if (ImGui::BeginChild("##SceneSettingsScroll", ImVec2(0, 0), ImGuiChildFlags_None)) {
			drawFn();
			ImGui::Spacing();  // Ensure bottom table border is visible when scrolled to end
		}
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::EndTable();
		ImGui::End();
		return true;
	}
}