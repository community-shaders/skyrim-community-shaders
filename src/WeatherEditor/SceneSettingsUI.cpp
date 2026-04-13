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

	// --- Shared helpers ---

	SourceGroup BuildSourceGroup(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		EntrySource sourceFilter, bool filterBySource)
	{
		SourceGroup group;
		for (size_t idx = 0; idx < entries.size(); ++idx) {
			const auto& e = entries[idx];
			if (filterBySource && e.source != sourceFilter)
				continue;
			int p = static_cast<int>(e.period);
			if (p < 0 || p >= kPeriodCount)
				continue;
			auto& featureMap = group.map[e.featureShortName];
			auto [it, inserted] = featureMap.try_emplace(e.settingKey);
			if (inserted) {
				it->second.fill(SIZE_MAX);
				group.order.push_back({ e.featureShortName, e.settingKey });
			}
			it->second[p] = idx;
		}
		std::sort(group.order.begin(), group.order.end());
		return group;
	}

	void DrawGroupSeparator(bool& firstGroup)
	{
		if (!firstGroup) {
			auto sepColor = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
			sepColor.w *= C::SCENE_GROUP_SEPARATOR_ALPHA;
			ImGui::PushStyleColor(ImGuiCol_Separator, sepColor);
			ImGui::Separator();
			ImGui::PopStyleColor();
		}
		firstGroup = false;
	}

	void SplitBySource(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		std::vector<size_t>& overwriteOut, std::vector<size_t>& userOut)
	{
		for (size_t i = 0; i < entries.size(); ++i)
			(entries[i].source == EntrySource::Overwrite ? overwriteOut : userOut).push_back(i);
	}

	void RemoveIndicesReversed(const std::vector<size_t>& indices, std::function<void(size_t)> removeFn)
	{
		auto sorted = indices;
		std::sort(sorted.begin(), sorted.end(), std::greater<>());
		for (auto idx : sorted)
			removeFn(idx);
	}

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

	void OpenAddDialog(SceneType type, AddSettingState& state)
	{
		state.Reset();
		state.dialogOpen = true;
		state.cachedFeatureNames = GetFeatureNamesForType(type);
	}

	void OpenWeatherAddDialog(RE::FormID /*weatherId*/, AddSettingState& state)
	{
		state.Reset();
		state.dialogOpen = true;
		state.cachedFeatureNames = SceneSettingsManager::GetExteriorRelevantFeatureNames();
	}

	// Core add-setting dialog: renders UI and delegates data ops to callbacks.
	static void DrawAddDialogCore(AddSettingState& state, Period period, bool addToAllPeriods,
		std::function<std::vector<std::string>()> featureNamesFn,
		std::function<std::vector<std::string>(const std::string&)> settingKeysFn,
		std::function<bool(const std::string&, const std::string&, Period)> isAddedFn,
		std::function<void(const std::string&, const std::string&, const json&, Period)> addFn)
	{
		if (!state.dialogOpen)
			return;

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(C::Em(C::SCENE_ADD_DIALOG_WIDTH_EM), 0));

		if (!Util::BeginWithRoundedClose("Add Feature Settings", &state.dialogOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			return;
		}

		if (state.cachedFeatureNames.empty())
			state.cachedFeatureNames = featureNamesFn();

		auto displayName = (state.selectedFeatureIdx >= 0 &&
							   state.selectedFeatureIdx < static_cast<int>(state.cachedFeatureNames.size())) ?
		                       SceneSettingsManager::GetFeatureDisplayName(state.cachedFeatureNames[state.selectedFeatureIdx]) :
		                       std::string("Select Feature...");

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##FeatureSelect", displayName.c_str())) {
			for (int i = 0; i < static_cast<int>(state.cachedFeatureNames.size()); ++i) {
				auto itemLabel = SceneSettingsManager::GetFeatureDisplayName(state.cachedFeatureNames[i]);
				if (ImGui::Selectable(itemLabel.c_str(), i == state.selectedFeatureIdx)) {
					state.selectedFeatureIdx = i;
					state.cachedSettingKeys = settingKeysFn(state.cachedFeatureNames[i]);
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

			if (ImGui::SmallButton("Select All"))
				std::fill(state.selectedSettings.begin(), state.selectedSettings.end(), true);
			ImGui::SameLine();
			if (ImGui::SmallButton("Select None"))
				std::fill(state.selectedSettings.begin(), state.selectedSettings.end(), false);

			ImGui::Spacing();

			auto& featureName = state.cachedFeatureNames[state.selectedFeatureIdx];
			if (ImGui::BeginChild("##SettingList", ImVec2(-FLT_MIN, C::Em(C::SCENE_ADD_LIST_HEIGHT_EM)), ImGuiChildFlags_Borders)) {
				for (int i = 0; i < static_cast<int>(state.cachedSettingKeys.size()); ++i) {
					auto& key = state.cachedSettingKeys[i];
					bool alreadyAdded = addToAllPeriods ? [&] { for (int p = 0; p < kPeriodCount; ++p) if (!isAddedFn(featureName, key, static_cast<Period>(p))) return false; return true; }() : isAddedFn(featureName, key, period);

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

			int selectedCount = 0;
			for (size_t i = 0; i < state.selectedSettings.size(); ++i)
				if (state.selectedSettings[i])
					++selectedCount;

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
								if (!isAddedFn(featureName, key, static_cast<Period>(p)))
									addFn(featureName, key, currentValue, static_cast<Period>(p));
						} else {
							if (!isAddedFn(featureName, key, period))
								addFn(featureName, key, currentValue, period);
						}
					}
					state.dialogOpen = false;
				}
			}
		}

		ImGui::End();
	}

	void DrawAddSettingDialog(SceneType type, AddSettingState& state, Period period, bool addToAllPeriods)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		DrawAddDialogCore(state, period, addToAllPeriods,
			[type]() { return GetFeatureNamesForType(type); },
			[type](const std::string& feat) { return (type == SceneType::TimeOfDay) ? SceneSettingsManager::GetTransitionableSettingKeys(feat) : SceneSettingsManager::GetFeatureSettingKeys(feat); },
			[type](const std::string& feat, const std::string& key, Period p) { return IsAlreadyAdded(type, feat, key, p); },
			[=](const std::string& feat, const std::string& key, const json& val, Period p) { manager->AddSetting(type, feat, key, val, p); });
	}

	void DrawWeatherAddDialog(RE::FormID weatherId, AddSettingState& state, Period period, bool addToAllPeriods)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		DrawAddDialogCore(state, period, addToAllPeriods,
			[]() { return SceneSettingsManager::GetExteriorRelevantFeatureNames(); },
			[](const std::string& feat) { return SceneSettingsManager::GetTransitionableSettingKeys(feat); },
			[=](const std::string& feat, const std::string& key, Period p) { return manager->HasWeatherEntryForPeriod(weatherId, feat, key, p); },
			[=](const std::string& feat, const std::string& key, const json& val, Period p) { manager->AddWeatherSetting(weatherId, feat, key, val, p); });
	}

	FlyoutResult DrawFlyoutControls(bool paused, bool isGroup)
	{
		FlyoutResult result;
		float frameH = ImGui::GetFrameHeight();
		float buttonH = frameH * C::FLYOUT_BUTTON_SCALE;
		float toggleH = buttonH * C::FLYOUT_TOGGLE_SCALE;
		float toggleOffsetY = (buttonH - toggleH) * 0.5f;

		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + toggleOffsetY));
		bool active = !paused;
		if (Util::SmallFeatureToggle(isGroup ? "##groupActive" : "##active", &active))
			result.toggled = true;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(paused ? (isGroup ? "Unpause all" : "Paused") : (isGroup ? "Pause all" : "Active"));

		ImGui::SameLine();
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, cursor.y));
		auto* menu = globals::menu;
		float iconH = frameH * C::FLYOUT_BUTTON_SCALE;
		float revertPad = iconH * C::FLYOUT_REVERT_PAD_SCALE;
		if (menu && Util::IconButton(isGroup ? "##revertAll" : "##revert", menu->uiIcons.undo.texture, ImVec2(iconH, iconH), revertPad))
			result.reverted = true;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(isGroup ? "Revert all to default" : "Revert to default");

		ImGui::SameLine();
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, cursor.y));
		if (Util::ThemedDeleteButton("X"))
			result.deleted = true;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(isGroup ? "Remove all" : "Remove");

		return result;
	}

	// Core value editor: renders the appropriate widget for a json value and calls updateFn on change.
	static void DrawValueEditorCore(const json& value, float inputWidth, bool readOnly,
		std::function<void(const json&)> updateFn, std::function<void()> commitFn)
	{
		auto settingType = SceneSettingsManager::DetectSettingType(value);
		ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : 0;

		switch (settingType) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = value.is_boolean() ? value.get<bool>() : (value.get<int>() != 0);
				if (readOnly)
					ImGui::BeginDisabled();
				if (ImGui::Checkbox("##val", &val) && !readOnly)
					updateFn(value.is_boolean() ? json(val) : json(val ? 1 : 0));
				if (readOnly)
					ImGui::EndDisabled();
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = value.is_number() ? value.get<float>() : 0.0f;
				if (!std::isfinite(val))
					val = 0.0f;
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputFloat("##val", &val, 0.0f, 0.0f, "%.3f", flags))
					if (!readOnly && std::isfinite(val))
						updateFn(json(val));
				if (!readOnly && ImGui::IsItemDeactivatedAfterEdit())
					commitFn();
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = value.get<int>();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputInt("##val", &val, 0, 0, flags))
					if (!readOnly)
						updateFn(json(val));
				if (!readOnly && ImGui::IsItemDeactivatedAfterEdit())
					commitFn();
			}
			break;
		default:
			ImGui::TextDisabled("(unsupported type)");
			break;
		}
	}

	void DrawValueEditor(SceneType type, size_t index, float inputWidth, bool readOnly)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entry = manager->GetEntries(type)[index];
		DrawValueEditorCore(entry.value, inputWidth, readOnly,
			[=](const json& v) { manager->UpdateEntryValue(type, index, v, true); },
			[=]() { manager->SaveAllUserSettings(); });
	}

	void DrawWeatherValueEditor(RE::FormID weatherId, size_t index, float inputWidth, bool readOnly)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entry = manager->GetWeatherConfig(weatherId).entries[index];
		DrawValueEditorCore(entry.value, inputWidth, readOnly,
			[=](const json& v) { manager->UpdateWeatherEntryValue(weatherId, index, v, true); },
			[=]() { manager->SaveAllUserSettings(); });
	}

	void DrawWeatherValueEditor(RE::FormID weatherId, const std::vector<size_t>& indices, float inputWidth, bool readOnly)
	{
		if (indices.empty())
			return;
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entry = manager->GetWeatherConfig(weatherId).entries[indices[0]];
		DrawValueEditorCore(entry.value, inputWidth, readOnly,
			[=](const json& v) { for (auto idx : indices) manager->UpdateWeatherEntryValue(weatherId, idx, v, true); },
			[=]() { manager->SaveAllUserSettings(); });
	}

	// Shared flyout state for DrawSettingEntry (one flyout visible at a time across all entries)
	static Util::FlyoutState entryFlyoutState;

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

		if (entry.paused)
			ImGui::BeginDisabled();

		DrawValueEditor(type, index, C::Em(C::SCENE_VALUE_INPUT_EM), isOverwrite);

		if (entry.paused)
			ImGui::EndDisabled();

		// Flyout on hover with toggle / revert / delete
		ImGuiID cellId = ImGui::GetItemID();
		bool wasDeleted = false;
		if (Util::BeginFlyout(entryFlyoutState, cellId)) {
			auto result = DrawFlyoutControls(entry.paused);

			if (result.toggled)
				manager->TogglePauseEntry(type, index);
			if (result.reverted)
				manager->RevertEntryToDefault(type, index);
			if (result.deleted) {
				if (isOverwrite) {
					popups.pendingDeleteIndex = index;
					popups.deleteSingleOverwrite.message = std::format(
						"Delete overwrite file '{}'?\nThis will permanently remove the file from disk.",
						entry.sourceFilename);
					popups.deleteSingleOverwrite.Request();
				} else {
					manager->RemoveSetting(type, index);
					wasDeleted = true;
				}
			}

			Util::EndFlyout(entryFlyoutState);
		}

		ImGui::PopID();
		return wasDeleted;
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
			RemoveIndicesReversed(popups.pendingDeleteRow, [&](size_t idx) {
				if (idx < manager->GetEntries(type).size())
					manager->RemoveSetting(type, idx);
			});
			popups.pendingDeleteRow.clear();
		}

		if (popups.deleteAllUser.Draw())
			manager->DeleteAllUserSettings(type);
	}

	static void DrawGroupedEntries(SceneType type, PopupState& popups,
		const std::vector<size_t>& indices)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(type);

		std::map<std::string, std::vector<size_t>> grouped;
		for (auto i : indices)
			if (i < entries.size())
				grouped[entries[i].featureShortName].push_back(i);

		for (auto& [_, featureIndices] : grouped)
			std::sort(featureIndices.begin(), featureIndices.end(), [&entries](size_t a, size_t b) {
				return entries[a].settingKey < entries[b].settingKey;
			});

		bool firstGroup = true;
		for (const auto& [featureName, featureIndices] : grouped) {
			DrawGroupSeparator(firstGroup);
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

		std::vector<size_t> overwriteIndices, userIndices;
		SplitBySource(entries, overwriteIndices, userIndices);

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