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

	void DrawAddSettingButton([[maybe_unused]] SceneType type, AddSettingState& state, [[maybe_unused]] Period period, const char* labelPrefix, [[maybe_unused]] bool addToAllPeriods)
	{
		if (labelPrefix) {
			ImGui::Text("%s", labelPrefix);
			ImGui::SameLine(C::Em(C::SCENE_TOD_LABEL_EM));
		}

		if (ImGui::Button("+", ImVec2(C::Em(C::SCENE_ADD_BUTTON_EM), C::Em(C::SCENE_ADD_BUTTON_EM)))) {
			state.Reset();
			state.dialogOpen = true;
			state.cachedFeatureNames = GetFeatureNamesForType(type);
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Add feature settings");
	}

	void RightAlignNextButton()
	{
		float btnSize = C::Em(C::SCENE_ADD_BUTTON_EM);
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnSize + ImGui::GetCursorPosX());
	}

	void DrawAddSettingDialog(SceneType type, AddSettingState& state, Period period, bool addToAllPeriods)
	{
		if (!state.dialogOpen)
			return;

		constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;
		auto* manager = SceneSettingsManager::GetSingleton();

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(C::Em(C::SCENE_ADD_DIALOG_WIDTH_EM), 0));

		if (!ImGui::Begin("Add Feature Settings", &state.dialogOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			return;
		}

		// Feature dropdown
		if (state.cachedFeatureNames.empty())
			state.cachedFeatureNames = GetFeatureNamesForType(type);

		auto displayName = (state.selectedFeatureIdx >= 0 &&
							   state.selectedFeatureIdx < static_cast<int>(state.cachedFeatureNames.size()))
		                     ? SceneSettingsManager::GetFeatureDisplayName(state.cachedFeatureNames[state.selectedFeatureIdx])
		                     : std::string("Select Feature...");

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##FeatureSelect", displayName.c_str())) {
			for (int i = 0; i < static_cast<int>(state.cachedFeatureNames.size()); ++i) {
				auto itemLabel = SceneSettingsManager::GetFeatureDisplayName(state.cachedFeatureNames[i]);
				if (ImGui::Selectable(itemLabel.c_str(), i == state.selectedFeatureIdx)) {
					state.selectedFeatureIdx = i;
					state.cachedSettingKeys = (type == SceneType::TimeOfDay)
					                            ? SceneSettingsManager::GetTransitionableSettingKeys(state.cachedFeatureNames[i])
					                            : SceneSettingsManager::GetFeatureSettingKeys(state.cachedFeatureNames[i]);
					state.selectedSettings.assign(state.cachedSettingKeys.size(), false);
				}
				if (i == state.selectedFeatureIdx)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		bool hasFeature = state.selectedFeatureIdx >= 0 && !state.cachedSettingKeys.empty();

		if (hasFeature) {
			ImGui::Spacing();
			ImGui::Separator();

			// Select All / Select None
			if (ImGui::SmallButton("Select All"))
				std::fill(state.selectedSettings.begin(), state.selectedSettings.end(), true);
			ImGui::SameLine();
			if (ImGui::SmallButton("Select None"))
				std::fill(state.selectedSettings.begin(), state.selectedSettings.end(), false);

			ImGui::Spacing();

			// Scrollable checkbox list
			auto& featureName = state.cachedFeatureNames[state.selectedFeatureIdx];
			if (ImGui::BeginChild("##SettingList", ImVec2(-FLT_MIN, C::Em(C::SCENE_ADD_LIST_HEIGHT_EM)), ImGuiChildFlags_Border)) {
				for (int i = 0; i < static_cast<int>(state.cachedSettingKeys.size()); ++i) {
					auto& key = state.cachedSettingKeys[i];
					bool alreadyAdded = addToAllPeriods
					                      ? [&] { for (int p = 0; p < kPeriodCount; ++p) if (!IsAlreadyAdded(type, featureName, key, static_cast<Period>(p))) return false; return true; }()
					                      : IsAlreadyAdded(type, featureName, key, period);

					if (alreadyAdded) {
						auto _ = Util::DisableGuard(true);
						bool checked = true;
						ImGui::Checkbox(key.c_str(), &checked);
					} else {
						bool sel = state.selectedSettings[i];
						if (ImGui::Checkbox(key.c_str(), &sel))
							state.selectedSettings[i] = sel;
					}
				}
			}
			ImGui::EndChild();

			ImGui::Spacing();

			// Count selected
			int selectedCount = 0;
			for (size_t i = 0; i < state.selectedSettings.size(); ++i)
				if (state.selectedSettings[i])
					++selectedCount;

			// Add button
			{
				auto _ = Util::DisableGuard(selectedCount == 0);
				auto label = std::format("Add ({})", selectedCount);
				if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) {
					for (size_t i = 0; i < state.cachedSettingKeys.size(); ++i) {
						if (!state.selectedSettings[i])
							continue;
						auto& key = state.cachedSettingKeys[i];
						auto currentValue = SceneSettingsManager::GetFeatureSettingValue(featureName, key);
						if (addToAllPeriods) {
							for (int p = 0; p < kPeriodCount; ++p)
								if (!IsAlreadyAdded(type, featureName, key, static_cast<Period>(p)))
									manager->AddSetting(type, featureName, key, currentValue, static_cast<Period>(p));
						} else {
							if (!IsAlreadyAdded(type, featureName, key, period))
								manager->AddSetting(type, featureName, key, currentValue, period);
						}
					}
					state.dialogOpen = false;
				}
			}
		}

		ImGui::End();
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

		bool firstGroup = true;
		for (const auto& [featureName, featureIndices] : grouped) {
			if (!firstGroup) {
				auto sepColor = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
				sepColor.w *= C::SCENE_GROUP_SEPARATOR_ALPHA;
				ImGui::PushStyleColor(ImGuiCol_Separator, sepColor);
				ImGui::Separator();
				ImGui::PopStyleColor();
			}
			firstGroup = false;

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