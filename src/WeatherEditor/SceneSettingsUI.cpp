#include "SceneSettingsUI.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include "../Globals.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"
#include "../Utils/Format.h"

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
			if (p < 0)
				continue;
			if (p >= kPeriodCount)
				p = 0;  // Period-less entries (InteriorOnly) map to slot 0
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

	using SettingEntry = SceneSettingsManager::SettingEntry;
	using OverrideKey = std::tuple<std::string, std::string, int>;

	static OverrideKey MakeOverrideKey(const SettingEntry& entry)
	{
		return { entry.featureShortName, entry.settingKey, static_cast<int>(entry.period) };
	}

	static std::set<OverrideKey> BuildActiveOverrideSet(const std::vector<SettingEntry>& entries)
	{
		std::set<OverrideKey> overrides;
		for (const auto& entry : entries)
			if (entry.source == EntrySource::Overwrite && !entry.paused)
				overrides.insert(MakeOverrideKey(entry));
		return overrides;
	}

	static bool IsOverridden(const std::set<OverrideKey>& overrides, const SettingEntry& entry)
	{
		return !overrides.empty() && overrides.contains(MakeOverrideKey(entry));
	}

	static bool HasOverriddenUserEntries(const std::vector<SettingEntry>& entries)
	{
		auto overrides = BuildActiveOverrideSet(entries);
		return std::any_of(entries.begin(), entries.end(),
			[&](const auto& entry) { return entry.source == EntrySource::User && IsOverridden(overrides, entry); });
	}

	/// Request a confirmation popup for deleting overwrite entries by indices.
	static void RequestOverwriteRowDelete(PopupState& popups,
		const std::vector<SceneSettingsManager::SettingEntry>& entries,
		const std::vector<size_t>& indices)
	{
		std::set<std::string> filenames;
		for (auto idx : indices)
			if (idx < entries.size())
				filenames.insert(entries[idx].sourceFilename);
		std::string fileList;
		for (const auto& f : filenames) {
			if (!fileList.empty())
				fileList += ", ";
			fileList += "'" + f + "'";
		}
		popups.pendingDeleteRow = indices;
		popups.deleteRowOverwrite.message = std::format(
			"Delete overwrite entries from {}?\nThis will permanently remove the file(s) from disk.",
			fileList);
		popups.deleteRowOverwrite.Request();
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
		std::function<std::vector<std::string>(const std::string&)> settingKeysFn,
		std::function<bool(const std::string&, const std::string&, Period)> isAddedFn,
		std::function<void(const std::string&, const std::string&, const json&, Period)> addFn)
	{
		if (!state.dialogOpen)
			return;

		ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(C::Em(C::SCENE_ADD_DIALOG_WIDTH_EM), 0));
		auto windowTitle = std::format("Add Feature Settings##{:x}", reinterpret_cast<uintptr_t>(&state));

		if (!Util::BeginWithRoundedClose(windowTitle.c_str(), &state.dialogOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			return;
		}

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

				auto prettyKey = Util::PrettifyIdentifier(key);
				if (alreadyAdded) {
					auto _ = Util::DisableGuard(true);
					bool checked = true;
					ImGui::Checkbox(prettyKey.c_str(), &checked);
				} else {
					bool sel = state.selectedSettings[i];
					if (ImGui::Checkbox(prettyKey.c_str(), &sel))
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
			[type](const std::string& feat) { return (type == SceneType::TimeOfDay) ? SceneSettingsManager::GetTransitionableSettingKeys(feat) : SceneSettingsManager::GetFeatureSettingKeys(feat); },
			[type](const std::string& feat, const std::string& key, Period p) { return IsAlreadyAdded(type, feat, key, p); },
			[=](const std::string& feat, const std::string& key, const json& val, Period p) { manager->AddSetting(type, feat, key, val, p); });
	}

	void DrawWeatherAddDialog(RE::FormID weatherId, AddSettingState& state, Period period, bool addToAllPeriods)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		DrawAddDialogCore(state, period, addToAllPeriods,
			[](const std::string& feat) { return SceneSettingsManager::GetTransitionableSettingKeys(feat); },
			[=](const std::string& feat, const std::string& key, Period p) { return manager->HasWeatherEntryForPeriod(weatherId, feat, key, p); },
			[=](const std::string& feat, const std::string& key, const json& val, Period p) { manager->AddWeatherSetting(weatherId, feat, key, val, p); });
	}

	FlyoutResult DrawFlyoutControls(bool paused, bool isGroup, bool isOverwrite)
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
		if (isOverwrite)
			ImGui::BeginDisabled();
		if (menu && Util::IconButton(isGroup ? "##revertAll" : "##revert", menu->uiIcons.undo.texture, ImVec2(iconH, iconH), revertPad))
			result.reverted = true;
		if (!isOverwrite) {
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(isGroup ? "Revert all to default" : "Revert to default");
		}
		if (isOverwrite)
			ImGui::EndDisabled();

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
		int readOnlyStyleColors = 0;

		if (readOnly) {
			// Save alpha before/after BeginDisabled to compute our contribution.
			// Nested BeginDisabled (e.g. when paused) won't change alpha, so boost = 1.0 (no counteraction).
			float alphaBefore = ImGui::GetStyle().Alpha;
			ImGui::BeginDisabled();
			float alphaAfter = ImGui::GetStyle().Alpha;
			float boost = (alphaAfter > 0.0f) ? alphaBefore / alphaAfter : 1.0f;

			if (settingType == SceneSettingsManager::SettingType::Boolean) {
				// Boost checkmark alpha to counteract only our disabled dimming
				ImVec4 cm = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
				cm.w *= boost;
				ImGui::PushStyleColor(ImGuiCol_CheckMark, cm);
				readOnlyStyleColors = 1;
			} else {
				// Transparent frame so overwrite values look like plain text
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
				// Boost text alpha to counteract only our disabled dimming
				ImVec4 tc = ImGui::GetStyleColorVec4(ImGuiCol_Text);
				tc.w *= boost;
				ImGui::PushStyleColor(ImGuiCol_Text, tc);
				readOnlyStyleColors = 5;
			}
		}

		switch (settingType) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = value.is_boolean() ? value.get<bool>() : (value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val) && !readOnly)
					updateFn(value.is_boolean() ? json(val) : json(val ? 1 : 0));
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = value.is_number() ? value.get<float>() : 0.0f;
				if (!std::isfinite(val))
					val = 0.0f;
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputFloat("##val", &val, 0.0f, 0.0f, "%.3f") && !readOnly)
					if (std::isfinite(val))
						updateFn(json(val));
				if (!readOnly && ImGui::IsItemDeactivatedAfterEdit())
					commitFn();
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = value.get<int>();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputInt("##val", &val, 0, 0) && !readOnly)
					updateFn(json(val));
				if (!readOnly && ImGui::IsItemDeactivatedAfterEdit())
					commitFn();
			}
			break;
		default:
			ImGui::TextDisabled("(unsupported type)");
			break;
		}

		if (readOnly) {
			ImGui::PopStyleColor(readOnlyStyleColors);
			ImGui::EndDisabled();
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

	void DrawSourceTable(
		const SourceGroup& group,
		const std::vector<SceneSettingsManager::SettingEntry>& entries,
		const char* tableId,
		EntrySource source,
		int numValueColumns,
		PopupState* popups,
		TableFlyoutState& flyout,
		const TableCallbacks& cb)
	{
		bool isOverwrite = source == EntrySource::Overwrite;
		bool multiColumn = numValueColumns > 1;
		int totalCols = multiColumn ? 1 + numValueColumns : 2;

		// Pre-collect per-column indices for header controls (multi-column only)
		std::array<std::vector<size_t>, kPeriodCount> perColumn{};
		if (multiColumn) {
			for (const auto& [_, featureMap] : group.map)
				for (const auto& [__, perKey] : featureMap)
					for (int p = 0; p < numValueColumns; ++p)
						if (perKey[p] != SIZE_MAX)
							perColumn[p].push_back(perKey[p]);
		}

		if (!ImGui::BeginTable(tableId, totalCols,
				ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
			return;

		// Column setup
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PARAM_COL_EM));
		if (multiColumn) {
			for (int i = 0; i < numValueColumns; ++i)
				ImGui::TableSetupColumn(SceneSettingsManager::kPeriodNames[i],
					ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PERIOD_COL_EM));
		} else {
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PERIOD_COL_EM) * 1.25f);
		}
		if (multiColumn) {
			ImGui::TableSetupScrollFreeze(0, 1);

			// Header row
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableSetColumnIndex(0);
			ImGui::TableHeader("Setting");

			for (int i = 0; i < numValueColumns; ++i) {
				ImGui::TableSetColumnIndex(1 + i);
				ImVec2 cellMin = ImGui::GetCursorScreenPos();
				float colW = ImGui::GetContentRegionAvail().x;
				ImGui::Text("%s", SceneSettingsManager::kPeriodNames[i]);
				ImVec2 cellMax(cellMin.x + colW, ImGui::GetItemRectMax().y);

				const auto& indices = perColumn[i];
				if (!indices.empty()) {
					ImGui::PushID(i);
					ImGuiID colId = ImGui::GetID("##colFlyout");
					if (Util::BeginFlyout(flyout.col, colId, cellMin, cellMax)) {
						bool allPaused = std::all_of(indices.begin(), indices.end(),
							[&](size_t idx) { return idx < entries.size() && entries[idx].paused; });
						auto result = DrawFlyoutControls(allPaused, true, isOverwrite);

						if (result.toggled)
							for (auto idx : indices)
								if (idx < entries.size() && entries[idx].paused == allPaused)
									cb.togglePause(idx);
						if (result.reverted)
							for (auto idx : indices)
								cb.revert(idx);
						if (result.deleted) {
							if (popups && isOverwrite)
								RequestOverwriteRowDelete(*popups, entries, indices);
							else
								RemoveIndicesReversed(indices, cb.remove);
							flyout.col.isOpen = false;
							flyout.col.activeId = 0;
						}
						Util::EndFlyout(flyout.col);
					}
					ImGui::PopID();
				}
			}
		}

		// Data rows
		auto& theme = globals::menu->GetSettings().Theme;
		std::string lastFeature;

		auto overrideSet = (source == EntrySource::User) ? BuildActiveOverrideSet(entries) : std::set<OverrideKey>{};

		for (const auto& sid : group.order) {
			// Feature header
			if (sid.feature != lastFeature) {
				lastFeature = sid.feature;
				ImGui::TableNextRow();
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
				ImGui::TableSetColumnIndex(0);
				ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "%s:",
					SceneSettingsManager::GetFeatureDisplayName(sid.feature).c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			auto mapIt = group.map.find(sid.feature);
			if (mapIt == group.map.end())
				continue;
			auto keyIt = mapIt->second.find(sid.key);
			if (keyIt == mapIt->second.end())
				continue;
			const auto& perKey = keyIt->second;

			// Collect valid indices for this row
			// In single-column mode, collect from ALL period slots (entries may span multiple periods)
			std::vector<size_t> rowIndices;
			int scanCols = multiColumn ? numValueColumns : kPeriodCount;
			for (int p = 0; p < scanCols; ++p)
				if (perKey[p] != SIZE_MAX)
					rowIndices.push_back(perKey[p]);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImVec2 cellStart = ImGui::GetCursorScreenPos();
			float cellWidth = ImGui::GetContentRegionAvail().x;
			const float labelRowStartY = ImGui::GetCursorPosY();

			ImGui::PushID(sid.key.c_str());
			ImGui::PushID(sid.feature.c_str());

			ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);

			float textBottomY = 0.0f;
			{
				auto text = Util::PrettifyIdentifier(sid.key);
				float wrapWidth = ImGui::GetContentRegionAvail().x;
				float lineH = ImGui::GetTextLineHeight();
				float fixedH = lineH * C::SCENE_SETTING_MAX_LINES;
				ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);

				if (textSize.y > fixedH + 0.5f) {
					// Text overflows — find where line 2 ends and truncate with "..."
					auto* font = ImGui::GetFont();
					const char* textEnd = text.c_str() + text.size();
					const char* lineStart = text.c_str();
					for (int line = 0; line < static_cast<int>(C::SCENE_SETTING_MAX_LINES); ++line)
						lineStart = font->CalcWordWrapPositionA(1.0f, lineStart, textEnd, wrapWidth);
					// Trim back for "..." and rebuild
					size_t visibleLen = static_cast<size_t>(lineStart - text.c_str());
					while (visibleLen > 3 && ImGui::CalcTextSize(text.substr(0, visibleLen).append("...").c_str(), nullptr, false, wrapWidth).y > fixedH + 0.5f)
						--visibleLen;
					text = text.substr(0, visibleLen) + "...";
				}

				ImGui::TextWrapped("%s", text.c_str());
				textBottomY = ImGui::GetItemRectMax().y;
				// Pad to fixed height so all rows are consistent
				float usedH = ImGui::GetItemRectSize().y;
				if (usedH < fixedH) {
					float pad = fixedH - usedH - ImGui::GetStyle().ItemSpacing.y;
					if (pad > 0.0f)
						ImGui::Dummy(ImVec2(0, pad));
				}
			}

			ImGui::SetWindowFontScale(1.0f);
			const float labelContentH = ImGui::GetCursorPosY() - labelRowStartY;

			// Row-level flyout (only for multi-column to avoid duplicate controls)
			if (multiColumn) {
				ImGuiID rowId = ImGui::GetID("##rowFlyout");
				// Hover area covers full row for detection
				ImVec2 hoverMin = cellStart;
				float rowH = std::max(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetItemRectMax().y - cellStart.y);
				ImVec2 hoverMax(cellStart.x + cellWidth, cellStart.y + rowH);
				// Flyout anchors to text bottom so it appears right under the text
				ImVec2 anchorMax(hoverMax.x, textBottomY);
				if (Util::BeginFlyout(flyout.row, rowId, hoverMin, hoverMax, anchorMax)) {
					bool allPaused = std::all_of(rowIndices.begin(), rowIndices.end(),
						[&](size_t i) { return i < entries.size() && entries[i].paused; });
					auto result = DrawFlyoutControls(allPaused, true, isOverwrite);

					if (result.toggled)
						for (auto idx : rowIndices)
							if (idx < entries.size() && entries[idx].paused == allPaused)
								cb.togglePause(idx);
					if (result.reverted)
						for (auto idx : rowIndices)
							cb.revert(idx);
					if (result.deleted) {
						if (popups && isOverwrite)
							RequestOverwriteRowDelete(*popups, entries, rowIndices);
						else
							RemoveIndicesReversed(rowIndices, cb.remove);
						flyout.row.isOpen = false;
						flyout.row.activeId = 0;
					}
					Util::EndFlyout(flyout.row);
				}
			}

			ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));

			// Value columns
			if (multiColumn) {
				for (int p = 0; p < numValueColumns; ++p) {
					ImGui::TableSetColumnIndex(1 + p);
					size_t entryIndex = perKey[p];

					if (entryIndex == SIZE_MAX) {
						if (source == EntrySource::User && cb.onAddPeriod) {
							ImGui::PushID(p);
							const float btnSz = C::Em(C::SCENE_ADD_PERIOD_BTN_EM);
							const float cellW = ImGui::GetContentRegionAvail().x;
							const float visualH = labelContentH - ImGui::GetStyle().ItemSpacing.y;
							ImGui::SetCursorPos(ImVec2(
								ImGui::GetCursorPosX() + std::max(0.f, (cellW - btnSz) * 0.5f),
								ImGui::GetCursorPosY() + std::max(0.f, (visualH - btnSz) * 0.5f)));
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
							if (ImGui::Button("+", ImVec2(btnSz, btnSz)))
								cb.onAddPeriod(sid.feature, sid.key, p);
							ImGui::PopStyleVar();
							ImGui::PopID();
						} else {
							ImGui::TextDisabled("--");
						}
						continue;
					}

					const auto& entry = entries[entryIndex];
					ImGui::PushID(static_cast<int>(entryIndex));

					if (entry.paused)
						ImGui::BeginDisabled();

					bool isOverridden = IsOverridden(overrideSet, entry);
					if (isOverridden) {
						const auto& ec = theme.StatusPalette.Error;
						ImGui::PushStyleColor(ImGuiCol_Text, ec);
						ImGui::PushStyleColor(ImGuiCol_CheckMark, ec);
					}

					cb.drawEditor(entryIndex, ImGui::GetContentRegionAvail().x, entry.source == EntrySource::Overwrite);

					if (isOverridden)
						ImGui::PopStyleColor(2);

					if (entry.paused)
						ImGui::EndDisabled();

					// Cell flyout
					ImGuiID cellId = ImGui::GetItemID();
					if (Util::BeginFlyout(flyout.cell, cellId)) {
						auto result = DrawFlyoutControls(entry.paused, false, isOverwrite);

						if (result.toggled)
							cb.togglePause(entryIndex);
						if (result.reverted)
							cb.revert(entryIndex);
						if (result.deleted) {
							if (popups && entry.source == EntrySource::Overwrite) {
								popups->pendingDeleteIndex = entryIndex;
								popups->deleteSingleOverwrite.message = std::format(
									"Delete overwrite file '{}'?\nThis will permanently remove the file from disk.",
									entry.sourceFilename);
								popups->deleteSingleOverwrite.Request();
							} else {
								cb.remove(entryIndex);
							}
							flyout.cell.isOpen = false;
							flyout.cell.activeId = 0;
						}
						Util::EndFlyout(flyout.cell);
					}

					ImGui::PopID();
				}
			} else {
				// Single-column: collapsed view of all entries for this key
				ImGui::TableSetColumnIndex(1);

				if (rowIndices.empty()) {
					ImGui::TextDisabled("--");
				} else {
					size_t displayIndex = rowIndices[0];
					bool anyPaused = std::any_of(rowIndices.begin(), rowIndices.end(),
						[&](size_t i) { return i < entries.size() && entries[i].paused; });

					ImGui::PushID(static_cast<int>(displayIndex));

					bool isOverridden = std::any_of(rowIndices.begin(), rowIndices.end(),
						[&](size_t i) { return i < entries.size() && IsOverridden(overrideSet, entries[i]); });

					if (anyPaused)
						ImGui::BeginDisabled();

					if (isOverridden) {
						const auto& ec = theme.StatusPalette.Error;
						ImGui::PushStyleColor(ImGuiCol_Text, ec);
						ImGui::PushStyleColor(ImGuiCol_CheckMark, ec);
					}

					if (cb.drawEditorMulti)
						cb.drawEditorMulti(rowIndices, ImGui::GetContentRegionAvail().x, isOverwrite);
					else
						cb.drawEditor(displayIndex, ImGui::GetContentRegionAvail().x, isOverwrite);

					if (isOverridden)
						ImGui::PopStyleColor(2);

					if (anyPaused)
						ImGui::EndDisabled();

					// Cell flyout operates on all row indices (centered for wide single-column)
					ImGuiID cellId = ImGui::GetItemID();
					if (Util::BeginFlyout(flyout.cell, cellId, true)) {
						bool allPaused = std::all_of(rowIndices.begin(), rowIndices.end(),
							[&](size_t i) { return i < entries.size() && entries[i].paused; });
						auto result = DrawFlyoutControls(allPaused, rowIndices.size() > 1, isOverwrite);

						if (result.toggled)
							for (auto idx : rowIndices)
								if (idx < entries.size() && entries[idx].paused == allPaused)
									cb.togglePause(idx);
						if (result.reverted)
							for (auto idx : rowIndices)
								cb.revert(idx);
						if (result.deleted) {
							if (popups && isOverwrite)
								RequestOverwriteRowDelete(*popups, entries, rowIndices);
							else
								RemoveIndicesReversed(rowIndices, cb.remove);
							flyout.cell.isOpen = false;
							flyout.cell.activeId = 0;
						}
						Util::EndFlyout(flyout.cell);
					}

					ImGui::PopID();
				}
			}

			// Suppress row flyout when a cell flyout is active to prevent accidental whole-row deletion
			if (flyout.cell.isOpen && !flyout.cell.closing && flyout.row.isOpen && !flyout.row.flyoutHovered)
				flyout.row.closing = true;

			ImGui::PopID();
			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	float GetSectionWidth(int numValueColumns)
	{
		auto& style = ImGui::GetStyle();
		bool multiColumn = numValueColumns > 1;
		int totalCols = multiColumn ? 1 + numValueColumns : 2;
		float colSum = C::Em(C::SCENE_TOD_PARAM_COL_EM);
		colSum += multiColumn ? numValueColumns * C::Em(C::SCENE_TOD_PERIOD_COL_EM)
		                      : C::Em(C::SCENE_TOD_PERIOD_COL_EM) * 1.25f;
		float tableWidth = colSum + totalCols * style.CellPadding.x * 2.0f + (totalCols + 1) * 1.0f;
		// Fewer value columns → header extends past table; at kPeriodCount columns it's flush
		float extraCols = C::SCENE_SECTION_HEADER_TARGET_COLS - numValueColumns;
		if (extraCols > 0.0f)
			tableWidth += extraCols * (C::Em(C::SCENE_TOD_PERIOD_COL_EM) + style.CellPadding.x * 2.0f + 1.0f);
		return tableWidth;
	}

	bool DrawSectionHeader(const char* label, const char* idSuffix,
		bool allPaused, std::function<void()> onTogglePause, std::function<void()> onDeleteAll,
		int numValueColumns, std::function<void()> onExportAll, bool hasActiveOverrides)
	{
		ImGui::Spacing();
		float w = GetSectionWidth(numValueColumns);
		ImGui::BeginChild(std::format("##sec{}", idSuffix).c_str(), ImVec2(w, 0), ImGuiChildFlags_AutoResizeY);
		auto headerLabel = std::format("{}{}", label, idSuffix);
		bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

		if (open) {
			if (hasActiveOverrides) {
				Util::Text::WrappedError("Feature values are being overridden. Pause overwrites to see changes.");
			}
			if (onExportAll) {
				if (ImGui::SmallButton(std::format("Export All{}", idSuffix).c_str()))
					onExportAll();
				ImGui::SameLine();
			}
			if (ImGui::SmallButton(std::format("{}{}", allPaused ? "Unpause All" : "Pause All", idSuffix).c_str()))
				onTogglePause();
			ImGui::SameLine();
			if (ImGui::SmallButton(std::format("Delete All{}", idSuffix).c_str()))
				onDeleteAll();
		}
		return open;
	}

	static bool DrawSelectedCheckbox(const std::string& label, uint8_t& selected)
	{
		bool checked = selected != 0;
		if (!ImGui::Checkbox(label.c_str(), &checked))
			return false;
		selected = checked ? 1 : 0;
		return true;
	}

	void EndSection()
	{
		ImGui::EndChild();
	}

	// Core export popup: list user entries with checkboxes (all on by default), then export on confirm.
	static void DrawExportPopupCore(
		const char* popupId,
		const std::vector<SceneSettingsManager::SettingEntry>& entries,
		ExportAllPopupState& state,
		std::function<void(const std::vector<size_t>&)> exportFn,
		bool showPeriod = true)
	{
		if (!state.dialogOpen)
			return;

		ImGui::OpenPopup(popupId);
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(C::Em(C::SCENE_ADD_DIALOG_WIDTH_EM), 0));

		if (!ImGui::BeginPopupModal(popupId, &state.dialogOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
			state.dialogOpen = false;
			return;
		}

		ImGui::TextUnformatted("Select settings to export as overwrite files:");
		ImGui::Spacing();

		if (ImGui::SmallButton("Select All"))
			std::fill(state.selected.begin(), state.selected.end(), uint8_t(1));
		ImGui::SameLine();
		if (ImGui::SmallButton("Select None"))
			std::fill(state.selected.begin(), state.selected.end(), uint8_t(0));

		ImGui::Spacing();
		if (ImGui::BeginChild("##ExportList", ImVec2(-FLT_MIN, C::Em(C::SCENE_ADD_LIST_HEIGHT_EM)), ImGuiChildFlags_Borders)) {
			if (showPeriod) {
				for (size_t i = 0; i < state.userIndices.size(); ++i) {
					auto idx = state.userIndices[i];
					if (idx >= entries.size())
						continue;
					const auto& e = entries[idx];
					auto label = e.period != SceneSettingsManager::TimeOfDayPeriod::Count
						? std::format("{} \u2014 {} ({})", SceneSettingsManager::GetFeatureDisplayName(e.featureShortName),
							Util::PrettifyIdentifier(e.settingKey), SceneSettingsManager::GetPeriodName(e.period))
						: std::format("{} \u2014 {}", SceneSettingsManager::GetFeatureDisplayName(e.featureShortName),
							Util::PrettifyIdentifier(e.settingKey));
					DrawSelectedCheckbox(std::format("{}##exp{}", label, i), state.selected[i]);
				}
			} else {
				using GroupKey = std::pair<std::string, std::string>;
				std::map<GroupKey, std::vector<size_t>> groups;
				for (size_t i = 0; i < state.userIndices.size(); ++i) {
					auto idx = state.userIndices[i];
					if (idx < entries.size())
						groups[{ entries[idx].featureShortName, entries[idx].settingKey }].push_back(i);
				}
				for (auto& [gk, stateIs] : groups) {
					bool checked = std::all_of(stateIs.begin(), stateIs.end(), [&](size_t i) { return state.selected[i]; });
					auto label = std::format("{} \u2014 {}##expg{}{}",
						SceneSettingsManager::GetFeatureDisplayName(gk.first), Util::PrettifyIdentifier(gk.second),
						gk.first, gk.second);
					if (ImGui::Checkbox(label.c_str(), &checked))
						for (auto i : stateIs)
							state.selected[i] = checked ? 1 : 0;
				}
			}
		}
		ImGui::EndChild();

		ImGui::Spacing();

		int count = static_cast<int>(std::count_if(state.selected.begin(), state.selected.end(), [](uint8_t v) { return v != 0; }));
		{
			auto _ = Util::DisableGuard(count == 0);
			if (ImGui::Button(std::format("Export ({})", count).c_str(), ImVec2(-FLT_MIN, 0))) {
				std::vector<size_t> toExport;
				for (size_t i = 0; i < state.userIndices.size(); ++i)
					if (state.selected[i])
						toExport.push_back(state.userIndices[i]);
				exportFn(toExport);
				state.dialogOpen = false;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}

	void DrawExportAllPopup(SceneType type, const std::vector<SceneSettingsManager::SettingEntry>& entries, ExportAllPopupState& state)
	{
		DrawExportPopupCore("Export User Settings##scene", entries, state,
			[type](const std::vector<size_t>& indices) {
				SceneSettingsManager::GetSingleton()->ExportUserSettingsToOverwrites(type, indices);
			});
	}

	void DrawWeatherExportAllPopup(RE::FormID weatherId, const std::vector<SceneSettingsManager::SettingEntry>& entries, ExportAllPopupState& state, bool showTod)
	{
		auto popupId = std::format("Export User Settings##wx{:08X}", weatherId);
		DrawExportPopupCore(popupId.c_str(), entries, state,
			[weatherId](const std::vector<size_t>& indices) {
				SceneSettingsManager::GetSingleton()->ExportWeatherUserSettingsToOverwrites(weatherId, indices);
			}, showTod);
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

	// =========================================================================
	// Consolidated Panel Implementations
	// =========================================================================

	// --- Interior Only Panel ---

	static AddSettingState s_interiorAddState;
	static PopupState s_interiorPopups{
		"Are you sure you want to delete all interior-only overwrite files?\nThis cannot be undone.",
		"Are you sure you want to remove all user-added interior-only settings?"
	};
	static TableFlyoutState s_interiorTableFlyout;
	static ExportAllPopupState s_interiorExportState;

	void DrawInteriorOnlyPanel()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(SceneType::InteriorOnly);
		auto& theme = globals::menu->GetSettings().Theme;

		ImGui::Text("Interior Only Settings");
		ImGui::Separator();

		if (ImGui::SmallButton("Add Setting"))
			OpenAddDialog(SceneType::InteriorOnly, s_interiorAddState);

		DrawPopups(SceneType::InteriorOnly, s_interiorPopups);
		DrawAddSettingDialog(SceneType::InteriorOnly, s_interiorAddState);

		if (entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No interior-only settings configured.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the Add Setting button above to add overrides.");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"Settings added here will override feature defaults when you enter an interior cell. "
				"Values revert automatically when you exit.");
			return;
		}

		ImGui::Spacing();

		TableCallbacks cb{
			[](size_t idx, float w, bool ro) { DrawValueEditor(SceneType::InteriorOnly, idx, w, ro); },
			nullptr,
			[](size_t idx) { SceneSettingsManager::GetSingleton()->TogglePauseEntry(SceneType::InteriorOnly, idx); },
			[](size_t idx) { SceneSettingsManager::GetSingleton()->RevertEntryToDefault(SceneType::InteriorOnly, idx); },
			[](size_t idx) { SceneSettingsManager::GetSingleton()->RemoveSetting(SceneType::InteriorOnly, idx); }
		};

		auto overwriteGroup = BuildSourceGroup(entries, EntrySource::Overwrite);
		auto userGroup = BuildSourceGroup(entries, EntrySource::User);

		if (!overwriteGroup.order.empty()) {
			if (DrawSectionHeader("Overwrite Files", "##iow", manager->AreAllOverwritesPaused(SceneType::InteriorOnly), [&] { manager->SetAllOverwritesPaused(SceneType::InteriorOnly, !manager->AreAllOverwritesPaused(SceneType::InteriorOnly)); }, [&] { s_interiorPopups.deleteAllOverwrites.Request(); }, 1))
				DrawSourceTable(overwriteGroup, entries, "##InteriorOW", EntrySource::Overwrite, 1, &s_interiorPopups, s_interiorTableFlyout, cb);
			EndSection();
		}

		if (!userGroup.order.empty()) {
			std::vector<size_t> owTmp, userIndices;
			SplitBySource(entries, owTmp, userIndices);
			if (DrawSectionHeader("User Settings", "##iusr", manager->AreAllUserPaused(SceneType::InteriorOnly),
					[&] { manager->SetAllUserPaused(SceneType::InteriorOnly, !manager->AreAllUserPaused(SceneType::InteriorOnly)); },
					[&] { s_interiorPopups.deleteAllUser.Request(); }, 1,
					[&] { s_interiorExportState.Open(userIndices); }, HasOverriddenUserEntries(entries)))
				DrawSourceTable(userGroup, entries, "##InteriorUsr", EntrySource::User, 1, &s_interiorPopups, s_interiorTableFlyout, cb);
			EndSection();
		}
		DrawExportAllPopup(SceneType::InteriorOnly, entries, s_interiorExportState);
	}

	// --- Time of Day Panel ---

	static AddSettingState s_todPeriodAddState[kPeriodCount];
	static AddSettingState s_todAllPeriodsAddState;
	static PopupState s_todPopups{
		"Are you sure you want to delete all time-of-day overwrite files?\nThis cannot be undone.",
		"Are you sure you want to remove all user-added time-of-day settings?"
	};
	static TableFlyoutState s_todTableFlyout;
	static ExportAllPopupState s_todExportState;

	void DrawTimeOfDayPanel()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(SceneType::TimeOfDay);
		auto& theme = globals::menu->GetSettings().Theme;

		ImGui::Text("Time of Day Settings");
		ImGui::SameLine();
		ImGui::TextDisabled("(Exterior Only)");

		auto currentPeriod = SceneSettingsManager::GetCurrentPeriod();
		ImGui::SameLine();
		ImGui::TextColored(theme.StatusPalette.InfoColor, "[%s %.1fh]",
			SceneSettingsManager::GetPeriodName(currentPeriod),
			SceneSettingsManager::GetCurrentGameHour());

		ImGui::Separator();

		for (int i = 0; i < kPeriodCount; ++i) {
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(i);
			auto label = std::format("Add {}", SceneSettingsManager::kPeriodNames[i]);
			if (ImGui::SmallButton(label.c_str()))
				OpenAddDialog(SceneType::TimeOfDay, s_todPeriodAddState[i]);
			ImGui::PopID();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Add All"))
			OpenAddDialog(SceneType::TimeOfDay, s_todAllPeriodsAddState);

		for (int i = 0; i < kPeriodCount; ++i)
			DrawAddSettingDialog(SceneType::TimeOfDay, s_todPeriodAddState[i], static_cast<Period>(i));
		DrawAddSettingDialog(SceneType::TimeOfDay, s_todAllPeriodsAddState, Period::Count, true);

		ImGui::Separator();

		DrawPopups(SceneType::TimeOfDay, s_todPopups);

		if (entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No time-of-day settings configured.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the Add buttons above to add overrides for each period.");
			return;
		}

		ImGui::Spacing();

		TableCallbacks cb{
			[](size_t idx, float w, bool ro) { DrawValueEditor(SceneType::TimeOfDay, idx, w, ro); },
			nullptr,
			[](size_t idx) { SceneSettingsManager::GetSingleton()->TogglePauseEntry(SceneType::TimeOfDay, idx); },
			[](size_t idx) { SceneSettingsManager::GetSingleton()->RevertEntryToDefault(SceneType::TimeOfDay, idx); },
			[](size_t idx) { SceneSettingsManager::GetSingleton()->RemoveSetting(SceneType::TimeOfDay, idx); },
			[](const std::string& feat, const std::string& key, int p) {
				SceneSettingsManager::GetSingleton()->AddSetting(SceneType::TimeOfDay, feat, key,
					SceneSettingsManager::GetFeatureSettingValue(feat, key), static_cast<Period>(p));
			}
		};

		auto overwriteGroup = BuildSourceGroup(entries, EntrySource::Overwrite);
		auto userGroup = BuildSourceGroup(entries, EntrySource::User);

		if (!overwriteGroup.order.empty()) {
			if (DrawSectionHeader("Overwrite Files", "##tow", manager->AreAllOverwritesPaused(SceneType::TimeOfDay), [&] { manager->SetAllOverwritesPaused(SceneType::TimeOfDay, !manager->AreAllOverwritesPaused(SceneType::TimeOfDay)); }, [&] { s_todPopups.deleteAllOverwrites.Request(); }, kPeriodCount))
				DrawSourceTable(overwriteGroup, entries, "##TODOverwrite", EntrySource::Overwrite, kPeriodCount, &s_todPopups, s_todTableFlyout, cb);
			EndSection();
		}

		if (!userGroup.order.empty()) {
			std::vector<size_t> owTmp, userIndices;
			SplitBySource(entries, owTmp, userIndices);
			if (DrawSectionHeader("User Settings", "##tusr", manager->AreAllUserPaused(SceneType::TimeOfDay),
					[&] { manager->SetAllUserPaused(SceneType::TimeOfDay, !manager->AreAllUserPaused(SceneType::TimeOfDay)); },
					[&] { s_todPopups.deleteAllUser.Request(); }, kPeriodCount,
					[&] { s_todExportState.Open(userIndices); }, HasOverriddenUserEntries(entries)))
				DrawSourceTable(userGroup, entries, "##TODUser", EntrySource::User, kPeriodCount, &s_todPopups, s_todTableFlyout, cb);
			EndSection();
		}
		DrawExportAllPopup(SceneType::TimeOfDay, entries, s_todExportState);
	}

	// --- Weather Scene Panel ---

	struct WeatherPanelState
	{
		AddSettingState periodAddStates[kPeriodCount];
		AddSettingState allPeriodsAddState;
		TableFlyoutState tableFlyout;
		ExportAllPopupState exportState;
	};
	static std::map<RE::FormID, WeatherPanelState> s_weatherPanelStates;

	static WeatherPanelState& GetWeatherState(RE::FormID id) { return s_weatherPanelStates[id]; }

	static TableCallbacks MakeWeatherCallbacks(RE::FormID weatherId, bool flat)
	{
		return {
			[weatherId](size_t idx, float w, bool ro) { DrawWeatherValueEditor(weatherId, idx, w, ro); },
			flat ? std::function<void(const std::vector<size_t>&, float, bool)>(
					   [weatherId](const std::vector<size_t>& indices, float w, bool ro) { DrawWeatherValueEditor(weatherId, indices, w, ro); }) :
			       nullptr,
			[weatherId](size_t idx) { SceneSettingsManager::GetSingleton()->TogglePauseWeatherEntry(weatherId, idx); },
			[weatherId](size_t idx) { SceneSettingsManager::GetSingleton()->RevertWeatherEntryToDefault(weatherId, idx); },
			[weatherId](size_t idx) { SceneSettingsManager::GetSingleton()->RemoveWeatherSetting(weatherId, idx); },
			[weatherId](const std::string& feat, const std::string& key, int p) {
				SceneSettingsManager::GetSingleton()->AddWeatherSetting(weatherId, feat, key,
					SceneSettingsManager::GetFeatureSettingValue(feat, key), static_cast<Period>(p));
			}
		};
	}

	static void DrawWeatherSections(RE::FormID weatherId, WeatherPanelState& state, int numValueColumns)
	{
		bool showTod = numValueColumns > 1;
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;
		auto cb = MakeWeatherCallbacks(weatherId, numValueColumns == 1);

		std::vector<size_t> overwriteIndices, userIndices;
		SplitBySource(entries, overwriteIndices, userIndices);

		if (!overwriteIndices.empty()) {
			auto group = BuildSourceGroup(entries, EntrySource::Overwrite);
			bool allPaused = std::all_of(overwriteIndices.begin(), overwriteIndices.end(),
				[&](size_t i) { return entries[i].paused; });
			if (DrawSectionHeader("Overwrite Files", "##wow",
					allPaused,
					[&] { for (auto idx : overwriteIndices) if (entries[idx].paused == allPaused) manager->TogglePauseWeatherEntry(weatherId, idx); },
					[&] { RemoveIndicesReversed(overwriteIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); }); },
					numValueColumns))
				DrawSourceTable(group, entries, "##WxOverwrite", EntrySource::Overwrite, numValueColumns, nullptr, state.tableFlyout, cb);
			EndSection();
		}

		if (!userIndices.empty()) {
			auto group = BuildSourceGroup(entries, EntrySource::User);
			bool allPaused = std::all_of(userIndices.begin(), userIndices.end(),
				[&](size_t i) { return entries[i].paused; });
			if (DrawSectionHeader("User Settings", "##wusr",
					allPaused,
					[&] { for (auto idx : userIndices) if (entries[idx].paused == allPaused) manager->TogglePauseWeatherEntry(weatherId, idx); },
					[&] { RemoveIndicesReversed(userIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); }); },
					numValueColumns,
					[&] { state.exportState.Open(userIndices); }, HasOverriddenUserEntries(entries)))
				DrawSourceTable(group, entries, "##WxUser", EntrySource::User, numValueColumns, nullptr, state.tableFlyout, cb);
			EndSection();
		}
		DrawWeatherExportAllPopup(weatherId, entries, state.exportState, showTod);
	}

	void DrawWeatherScenePanel(RE::FormID weatherId)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		auto& state = GetWeatherState(weatherId);
		const auto& config = manager->GetWeatherConfig(weatherId);
		auto& theme = globals::menu->GetSettings().Theme;
		bool showTod = manager->IsWeatherShowTimeOfDay(weatherId);

		ImGui::Text("Scene Settings");
		ImGui::Separator();

		{
			bool toggled = showTod;
			if (ImGui::Checkbox("Time of Day", &toggled))
				manager->SetWeatherShowTimeOfDay(weatherId, toggled);
			showTod = toggled;
		}

		if (showTod) {
			for (int i = 0; i < kPeriodCount; ++i) {
				if (i > 0)
					ImGui::SameLine();
				ImGui::PushID(i);
				auto label = std::format("Add {}", SceneSettingsManager::kPeriodNames[i]);
				if (ImGui::SmallButton(label.c_str()))
					OpenWeatherAddDialog(weatherId, state.periodAddStates[i]);
				ImGui::PopID();
			}
			ImGui::SameLine();
		}

		if (ImGui::SmallButton(showTod ? "Add All" : "Add Setting"))
			OpenWeatherAddDialog(weatherId, state.allPeriodsAddState);

		DrawWeatherAddDialog(weatherId, state.allPeriodsAddState, Period::Count, true);
		if (showTod)
			for (int p = 0; p < kPeriodCount; ++p)
				DrawWeatherAddDialog(weatherId, state.periodAddStates[p], static_cast<Period>(p));

		if (config.entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No scene settings for this weather.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the Add buttons above to add overrides.");
			return;
		}

		DrawWeatherSections(weatherId, state, showTod ? kPeriodCount : 1);
	}
}
