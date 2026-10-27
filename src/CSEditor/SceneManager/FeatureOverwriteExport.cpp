#include "FeatureOverwritesPanel.h"

#include "BaselineManager.h"
#include "CSEditor/Browser/BrowserWidgets.h"
#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "SceneSettingsManager.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsCatalog.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#define I18N_KEY_PREFIX "feature.scene_manager.overwrites."

namespace
{
	/// Fixed ID behind the translated title, so the popup survives a language switch.
	constexpr const char* kPopupId = "###FeatureOverwritesExport";
	constexpr const char* kPickerId = "##ExistingNames";

	/// Sizes in font heights, so the modal scales with the UI font.
	constexpr float kModalWidthEm = 48.0f;
	constexpr float kLockedModalWidthEm = 36.0f;
	constexpr float kFeatureListWidthEm = 12.0f;
	constexpr float kListHeightEm = 16.0f;
	constexpr float kSearchWidthEm = 10.0f;
	constexpr float kNameWidthEm = 14.0f;
	constexpr float kValueColumnEm = 6.0f;

	constexpr size_t kNameBufferSize = 128;
	constexpr size_t kSearchBufferSize = 256;
	constexpr std::string_view kGroupSeparator = " / ";

	/// CheckboxFlags states: every bit set reads as ticked, some as mixed.
	constexpr int kTickNone = 0;
	constexpr int kTickSome = 1;
	constexpr int kTickAll = 3;

	using BrowserUI::FilterChip;
	using Util::Settings::FormatValue;

	/** @brief How the destination file holds a setting. */
	enum class FileState
	{
		Absent,
		Same,
		Different
	};

	/** @brief One exportable setting, sorted under its group. */
	struct SettingRow
	{
		std::string path;
		/// The label without its group.
		std::string name;
		std::string value;
		size_t group = 0;
		FileState fileState = FileState::Absent;
		std::string fileValue;
	};

	/** @brief One feature's settings, loaded on first view or tick, and how its destination file holds them. */
	struct FeatureExport
	{
		std::string shortName;
		std::string label;
		json values;
		std::vector<SettingRow> rows;
		std::vector<std::string> groups;
		std::vector<uint8_t> ticked;
		bool loaded = false;

		/// Which destination the file states below describe.
		std::string targetKey;
		std::filesystem::path destination;
		/// Null when the file exists but cannot be read.
		std::optional<json> existing;
		size_t fileCount = 0;
	};

	/** @brief The pack the export writes to; outlives the modal, so the next export lands in the same place. */
	struct Target
	{
		char name[kNameBufferSize]{};
	};
	Target target;

	/** @brief One opening of the modal. */
	struct Session
	{
		std::vector<FeatureExport> features;
		size_t viewed = 0;
		char search[kSearchBufferSize]{};
		std::optional<size_t> group;
		bool inFileOnly = false;
		bool removeUnticked = false;
		bool featureLocked = false;
		bool failed = false;
		Util::DeferredModal modal;
		std::vector<std::string> packNames;
	};
	Session session;

	/** @brief What an export would do, across every feature with a tick. */
	struct ExportCounts
	{
		size_t added = 0;
		size_t updated = 0;
		size_t unchanged = 0;
		size_t removed = 0;
		size_t files = 0;
		bool unreadable = false;

		size_t Ticked() const { return added + updated + unchanged; }
		size_t Changes() const { return added + updated + removed; }
	};

	/** @brief One settings-list line: a group heading over lines up to `end`, or a setting row. */
	struct Line
	{
		bool header = false;
		/// The group for a heading, else the row.
		size_t index = 0;
		size_t end = 0;
	};

	size_t CountTicked(const FeatureExport& entry)
	{
		return static_cast<size_t>(std::ranges::count(entry.ticked, uint8_t{ 1 }));
	}

	int GetTickState(size_t ticked, size_t total)
	{
		return ticked == 0 ? kTickNone : ticked == total ? kTickAll :
		                                                   kTickSome;
	}

	bool IsRemoved(const FeatureExport& entry, size_t index)
	{
		return session.removeUnticked && !entry.ticked[index] && entry.rows[index].fileState != FileState::Absent;
	}

	/** @brief Reads the feature's own values, not whatever the active scene is applying, and splits labels into groups. */
	void LoadFeature(FeatureExport& entry)
	{
		if (std::exchange(entry.loaded, true))
			return;
		auto* feature = Feature::FindFeatureByShortName(entry.shortName);
		if (!feature)
			return;
		{
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			feature->SaveSettings(entry.values);
		}
		for (auto& setting : Util::Settings::GetExportSettings(entry.shortName, entry.values)) {
			const auto split = setting.label.find(kGroupSeparator);
			const std::string group = split == std::string::npos ? T(TKEY("export.group_general"), "General") :
			                                                       setting.label.substr(0, split);
			auto found = std::ranges::find(entry.groups, group);
			if (found == entry.groups.end())
				found = entry.groups.insert(found, group);
			entry.rows.push_back({ .path = setting.path,
				.name = split == std::string::npos ? setting.label : setting.label.substr(split + kGroupSeparator.size()),
				.value = FormatValue(entry.values[json::json_pointer(setting.path)]),
				.group = static_cast<size_t>(found - entry.groups.begin()) });
		}
		// Labels sort ungrouped settings among the groups, so gather each group into one run.
		std::ranges::stable_sort(entry.rows, {}, &SettingRow::group);
		entry.ticked.assign(entry.rows.size(), uint8_t{ 0 });
	}

	/** @brief Compares the feature with its destination file whenever the target changes. */
	void RefreshTarget(FeatureExport& entry)
	{
		std::string key = target.name;
		if (entry.targetKey == key)
			return;
		entry.targetKey = std::move(key);
		entry.destination = BaselineManager::GetSingleton()->GetExportDestination(target.name, entry.shortName);
		entry.existing = entry.destination.empty() ? std::optional<json>(json::object()) : BaselineManager::ReadExportTarget(entry.destination);
		entry.fileCount = 0;
		for (auto& row : entry.rows) {
			row.fileState = FileState::Absent;
			row.fileValue.clear();
			const json::json_pointer pointer(row.path);
			if (!entry.existing || !entry.existing->contains(pointer))
				continue;
			row.fileValue = FormatValue(entry.existing->at(pointer));
			// Compared as shown, so float noise in hand-written files does not read as a change.
			row.fileState = row.fileValue == row.value ? FileState::Same : FileState::Different;
			++entry.fileCount;
		}
	}

	void CollectExistingNames()
	{
		for (const auto& pack : UnifiedPresetCatalog::GetSingleton().GetPacks())
			if (pack.source == UnifiedPresetCatalog::SourceKind::UnifiedPack)
				session.packNames.push_back(pack.id);
	}

	ExportCounts CountChanges()
	{
		ExportCounts counts;
		for (auto& entry : session.features) {
			if (CountTicked(entry) == 0)
				continue;
			RefreshTarget(entry);
			++counts.files;
			counts.unreadable |= !entry.existing;
			for (size_t index = 0; index < entry.rows.size(); ++index) {
				if (IsRemoved(entry, index))
					++counts.removed;
				else if (entry.ticked[index])
					switch (entry.rows[index].fileState) {
					case FileState::Absent:
						++counts.added;
						break;
					case FileState::Same:
						++counts.unchanged;
						break;
					default:
						++counts.updated;
						break;
					}
			}
		}
		return counts;
	}

	/** @brief Name field, the picker of packs already exported to, and the guide. */
	void DrawNameRow()
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(T(TKEY("export.pack_name"), "Pack Name"));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * kNameWidthEm);
		ImGui::InputText("##name", target.name, sizeof(target.name));
		const auto& names = session.packNames;
		Util::DropdownArrowButton(kPickerId, T(TKEY("export.existing_tooltip"), "Pick a name already in use to add to its file."),
			names.empty(), T(TKEY("export.existing_none_tooltip"), "Nothing exported under a name yet."));
		if (ImGui::BeginPopup(kPickerId)) {
			for (const auto& name : names)
				if (Util::Selectable(name.c_str(), name == target.name))
					strncpy_s(target.name, sizeof(target.name), name.c_str(), _TRUNCATE);
			ImGui::EndPopup();
		}
		BrowserUI::HelpMarkerRight(T(TKEY("export.guide_baseline"),
			"Writes feature settings into a Baseline preset pack, without any scene layer values. The pack applies when "
			"picked on the Presets page.\n\n"
			"Pack Name and its dropdown arrow: one file per feature is written into this pack; the arrow adds to a pack "
			"exported before.\n"
			"Settings: tick what to write. With several features, pick each one on the left. In file shows what the "
			"destination already holds.\n"
			"Remove unticked settings: deletes the settings you left unticked from the file instead of keeping them.\n\n"
			"Workflow: tune a feature in Base Settings, then export the settings the pack should ship. For a full look "
			"with scene layers, use Export Preset in the CS Editor (Ctrl+Shift+S)."));
	}

	/** @brief Where the viewed feature's file goes, and whether it is new. */
	void DrawDestination(const FeatureExport& entry)
	{
		ImGui::AlignTextToFramePadding();
		if (entry.destination.empty()) {
			Util::TextUnformattedDisabled(T(TKEY("export.name_required"), "Enter a name to see where the file goes."));
			return;
		}
		const auto [label, tint, tooltip] = !entry.existing ?
		                                        std::tuple{ T(TKEY("export.file_unreadable"), "Unreadable"), Util::Colors::GetError(),
													T(TKEY("export.file_unreadable_tooltip"), "The file exists but cannot be read, so it is left alone. Fix or remove it first.") } :
		                                    entry.existing->empty() ?
		                                        std::tuple{ T(TKEY("export.file_new"), "New file"), Util::Colors::GetSuccess(),
													T(TKEY("export.file_new_tooltip"), "Export creates this file.") } :
		                                        std::tuple{ T(TKEY("export.file_existing"), "Existing file"), Util::Colors::GetInfo(),
													T(TKEY("export.file_existing_tooltip"), "Export updates the ticked settings. Other settings and metadata in the file are kept.") };
		// A pack file is <presets>/<pack>/Baseline/<feature>.json.
		const auto root = entry.destination.parent_path().parent_path().parent_path();
		const auto shown = entry.destination.lexically_relative(root.parent_path()).generic_string();
		const float badgeWidth = Util::MeasureBadgeWidth(label) + ImGui::GetStyle().ItemSpacing.x;
		BrowserUI::MetaText(shown.c_str(), std::max(1.0f, ImGui::GetContentRegionAvail().x - badgeWidth));
		BrowserUI::InlineBadge(label, tint);
		Util::AddTooltip(tooltip);
	}

	/** @brief Every exportable feature with a tri-state tick; clicking a row shows its settings. */
	void DrawFeatureList()
	{
		const float em = ImGui::GetFontSize();
		if (!BrowserUI::BeginListTable("##features", 1, ImVec2(em * kFeatureListWidthEm, em * kListHeightEm), false, nullptr))
			return;
		// An auto-resizing modal fits columns to content, which the ellipsized label measures against.
		ImGui::TableSetupColumn("##feature", ImGuiTableColumnFlags_WidthStretch);
		for (size_t index = 0; index < session.features.size(); ++index) {
			auto& entry = session.features[index];
			ImGui::PushID(entry.shortName.c_str());
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (Util::TableRowSelectable("##row", index == session.viewed, BrowserUI::kTickRowFlags) && session.viewed != index) {
				session.viewed = index;
				session.group.reset();
			}
			ImGui::SameLine(0.0f, 0.0f);
			int state = GetTickState(CountTicked(entry), entry.rows.size());
			if (ImGui::CheckboxFlags("##tick", &state, kTickAll)) {
				LoadFeature(entry);
				std::ranges::fill(entry.ticked, uint8_t{ state == kTickAll });
			}
			ImGui::SameLine();
			BrowserUI::EllipsizedText(entry.label.c_str(), 0.0f, ImGui::GetStyleColorVec4(ImGuiCol_Text));
			ImGui::PopID();
		}
		BrowserUI::EndListTable();
	}

	/** @brief Search, a chip per group, and the in-file toggle. */
	void DrawSettingFilters(const FeatureExport& entry)
	{
		BrowserUI::SearchField("##search", session.search, sizeof(session.search), T("ui.search", "Search..."),
			ImGui::GetFontSize() * kSearchWidthEm, true);
		if (entry.groups.size() > 1) {
			ImGui::PushID("groups");
			const char* all = T("cs_editor.filter_all", "All");
			BrowserUI::SameLineIfFits(BrowserUI::MeasureChip(all, false));
			if (FilterChip("##all", { all }, !session.group))
				session.group.reset();
			for (size_t group = 0; group < entry.groups.size(); ++group) {
				ImGui::PushID(static_cast<int>(group));
				const char* label = entry.groups[group].c_str();
				BrowserUI::SameLineIfFits(BrowserUI::MeasureChip(label, false));
				if (FilterChip("##group", { label }, session.group == group))
					session.group = session.group == group ? std::nullopt : std::optional{ group };
				ImGui::PopID();
			}
			ImGui::PopID();
		}

		const auto count = static_cast<int>(entry.fileCount);
		const float width = BrowserUI::MeasureToggleChip(count);
		BrowserUI::SameLineIfFits(width);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
		const auto tooltip = std::format("{}\n{}", T(TKEY("export.in_file_only"), "In file only"),
			T(TKEY("export.in_file_only_tooltip"), "Lists only the settings the destination file already holds."));
		const Util::DisableGuard empty(count == 0 && !session.inFileOnly);
		if (BrowserUI::ToggleChip("##inFile", BrowserUI::GlyphIcon(Icons::TI(ICON_TI_FILE_EXPORT)), count,
				session.inFileOnly, Util::Colors::GetInfo(), tooltip.c_str()))
			session.inFileOnly = !session.inFileOnly;
	}

	std::vector<size_t> FilterRows(const FeatureExport& entry)
	{
		const std::string query = session.search;
		std::vector<size_t> visible;
		for (size_t index = 0; index < entry.rows.size(); ++index) {
			const auto& row = entry.rows[index];
			if ((session.group && row.group != *session.group) || (session.inFileOnly && row.fileState == FileState::Absent))
				continue;
			if (query.empty() || Util::StringMatchesSearch(row.name, query) || Util::StringMatchesSearch(entry.groups[row.group], query))
				visible.push_back(index);
		}
		return visible;
	}

	/** @brief Visible rows with a heading before each group, unless one group is picked or there is only one. */
	std::vector<Line> BuildLines(const FeatureExport& entry, const std::vector<size_t>& visible)
	{
		const bool headers = !session.group && entry.groups.size() > 1;
		std::vector<Line> lines;
		std::optional<size_t> heading;
		for (const auto index : visible) {
			const auto group = entry.rows[index].group;
			if (headers && (!heading || lines[*heading].index != group)) {
				heading = lines.size();
				lines.push_back({ .header = true, .index = group });
			}
			lines.push_back({ .index = index });
			if (heading)
				lines[*heading].end = lines.size();
		}
		return lines;
	}

	void DrawGroupHeader(FeatureExport& entry, std::span<const Line> members, const std::string& group)
	{
		const auto ticked = static_cast<size_t>(std::ranges::count_if(members, [&](const Line& line) { return entry.ticked[line.index] != 0; }));
		int state = GetTickState(ticked, members.size());
		if (ImGui::CheckboxFlags("##tick", &state, kTickAll))
			for (const auto& line : members)
				entry.ticked[line.index] = state == kTickAll;
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		BrowserUI::SectionLabel(group.c_str());
	}

	void DrawSettingRow(FeatureExport& entry, size_t index)
	{
		const auto& row = entry.rows[index];
		if (Util::TableRowSelectable("##row", false, BrowserUI::kTickRowFlags))
			entry.ticked[index] ^= 1;
		ImGui::SameLine(0.0f, 0.0f);
		bool ticked = entry.ticked[index] != 0;
		if (ImGui::Checkbox("##tick", &ticked))
			entry.ticked[index] = ticked;

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		BrowserUI::EllipsizedText(row.name.c_str(), 0.0f,
			ImGui::GetStyleColorVec4(IsRemoved(entry, index) ? ImGuiCol_TextDisabled : ImGuiCol_Text));

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		BrowserUI::EllipsizedText(row.value.c_str(), 0.0f, ImGui::GetStyleColorVec4(ImGuiCol_Text));

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		switch (row.fileState) {
		case FileState::Absent:
			Util::TextUnformattedDisabled("-");
			break;
		case FileState::Same:
			BrowserUI::EllipsizedText(row.fileValue.c_str(), 0.0f, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			Util::AddTooltip(T(TKEY("export.file_same_tooltip"), "The file already holds the current value."));
			break;
		default:
			BrowserUI::EllipsizedText(row.fileValue.c_str(), 0.0f, Util::Colors::GetInfo());
			Util::AddTooltip(T(TKEY("export.file_different_tooltip"), "Exporting replaces this with the current value."));
			break;
		}
	}

	void DrawSettingTable(FeatureExport& entry, const std::vector<Line>& lines)
	{
		const float em = ImGui::GetFontSize();
		if (!BrowserUI::BeginListTable("##settings", 4, ImVec2(0.0f, em * kListHeightEm), lines.empty(),
				T("cs_editor.scene_copy_empty", "Nothing matches.")))
			return;

		ImGui::TableSetupColumn("##tick", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn(T(TKEY("export.column_setting"), "Setting"), ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn(T(TKEY("export.column_value"), "Value"), ImGuiTableColumnFlags_WidthFixed, em * kValueColumnEm);
		ImGui::TableSetupColumn(T(TKEY("export.column_in_file"), "In file"), ImGuiTableColumnFlags_WidthFixed, em * kValueColumnEm);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		// Every line is frame-high, which the clipper's uniform-height assumption needs.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(lines.size()));
		while (clipper.Step()) {
			for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
				const auto& line = lines[index];
				ImGui::PushID(index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const auto first = static_cast<size_t>(index) + 1;
				if (line.header)
					DrawGroupHeader(entry, std::span(lines).subspan(first, line.end - first), entry.groups[line.index]);
				else
					DrawSettingRow(entry, line.index);
				ImGui::PopID();
			}
		}
		BrowserUI::EndListTable();
	}

	/** @brief Ticks every listed setting, and a count of the ticks, including hidden ones, that clears them. */
	void DrawSelectionFooter(FeatureExport& entry, const std::vector<size_t>& visible)
	{
		const auto selectedShown = static_cast<size_t>(std::ranges::count_if(visible, [&](size_t index) { return entry.ticked[index] != 0; }));
		const auto action = BrowserUI::SelectionFooter(CountTicked(entry), selectedShown,
			T(TKEY("export.clear_ticks_tooltip"), "Unticks every setting of this feature, shown or hidden."));
		if (action == BrowserUI::SelectionAction::SelectShown)
			for (const auto index : visible)
				entry.ticked[index] = 1;
		else if (action == BrowserUI::SelectionAction::Clear)
			std::ranges::fill(entry.ticked, uint8_t{ 0 });
	}

	void DrawSettings(FeatureExport& entry)
	{
		DrawSettingFilters(entry);
		const auto visible = FilterRows(entry);
		DrawSettingTable(entry, BuildLines(entry, visible));
		DrawSelectionFooter(entry, visible);
	}

	void DrawResults(const ExportCounts& counts)
	{
		bool first = true;
		const auto badge = [&first](const char* format, size_t count, const ImVec4& tint) {
			if (count == 0)
				return;
			if (!std::exchange(first, false))
				ImGui::SameLine();
			Util::Badge(std::vformat(format, std::make_format_args(count)).c_str(), tint);
		};
		badge(T(TKEY("export.result_new"), "{} new"), counts.added, Util::Colors::GetSuccess());
		badge(T(TKEY("export.result_updated"), "{} updated"), counts.updated, Util::Colors::GetInfo());
		badge(T(TKEY("export.result_unchanged"), "{} unchanged"), counts.unchanged, Util::Colors::GetSecondary());
		badge(T(TKEY("export.result_removed"), "{} removed"), counts.removed, Util::Colors::GetError());
		if (first)
			Util::TextUnformattedDisabled(T(TKEY("export.result_none"), "Tick the settings to export."));
	}

	/** @brief Writes one file per feature with a tick. @return False when any write failed. */
	bool RunExport()
	{
		auto* manager = BaselineManager::GetSingleton();
		// One file per feature, so a failure puts the ones already written back as they were.
		std::vector<std::pair<std::filesystem::path, std::optional<std::string>>> written;
		const auto rollBack = [&] {
			for (const auto& [path, previous] : written) {
				std::error_code ec;
				if (previous)
					Util::FileHelpers::WriteFileAtomically(path, *previous, "Baseline rollback");
				else
					std::filesystem::remove(path, ec);
			}
		};
		for (auto& entry : session.features) {
			if (CountTicked(entry) == 0)
				continue;
			std::vector<std::string> paths;
			std::vector<std::string> removePaths;
			for (size_t index = 0; index < entry.rows.size(); ++index) {
				if (entry.ticked[index])
					paths.push_back(entry.rows[index].path);
				else if (IsRemoved(entry, index))
					removePaths.push_back(entry.rows[index].path);
			}
			const auto destination = manager->GetExportDestination(target.name, entry.shortName);
			std::optional<std::string> previous;
			if (std::ifstream file(destination, std::ios::binary); file)
				previous.emplace(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			// Every file is compared afresh, the restored ones included.
			entry.targetKey.clear();
			if (!manager->ExportSettings(target.name, entry.shortName, paths, entry.values, removePaths)) {
				rollBack();
				return false;
			}
			written.emplace_back(destination, std::move(previous));
			// The exported value is now the baseline's, so the scene resolves over it as it will after a reopen.
			SceneSettingsManager::GetSingleton()->ReleaseSketches(entry.shortName);
		}
		auto& catalog = UnifiedPresetCatalog::GetSingleton();
		catalog.Discover();
		catalog.ReapplyIfInUse(Util::FileHelpers::SanitizeFileName(target.name));
		return true;
	}

	/** @brief Export with its count, or greyed with the reason, then Cancel. */
	void DrawButtons(const ExportCounts& counts)
	{
		const auto name = Util::FileHelpers::SanitizeFileName(target.name);
		const char* blocker = name.empty()          ? T(TKEY("export.blocked_name"), "Enter a name first.") :
		                      counts.Ticked() == 0  ? T(TKEY("export.blocked_ticks"), "Tick the settings to export.") :
		                      counts.unreadable     ? T(TKEY("export.blocked_unreadable"), "A destination file cannot be read. Fix or remove it first.") :
		                      counts.Changes() == 0 ? T(TKEY("export.blocked_unchanged"), "The file already holds these values.") :
		                                              nullptr;
		auto ticked = counts.Ticked();
		auto files = counts.files;
		const auto label = (files > 1 ? std::vformat(T(TKEY("export.count_files"), "Export {} to {} files"), std::make_format_args(ticked, files)) :
										std::vformat(T(TKEY("export.count"), "Export {}"), std::make_format_args(ticked))) +
		                   "###export";
		{
			const Util::DisableGuard blocked(blocker != nullptr);
			if (Util::Button(label.c_str())) {
				session.failed = !RunExport();
				if (!session.failed) {
					EditorWindow::GetSingleton()->ShowNotification(
						std::vformat(T(TKEY("export.done"), "Exported {} settings as {}"), std::make_format_args(ticked, name)),
						Util::Colors::GetSuccess());
					ImGui::CloseCurrentPopup();
				}
			}
		}
		Util::AddTooltip(blocker ? blocker : T(TKEY("export.ready_tooltip"), "Writes the ticked settings without scene-specific values."),
			Util::kTooltipWhenDisabled);
		ImGui::SameLine();
		if (Util::Button(T("cs_editor.cancel", "Cancel")))
			ImGui::CloseCurrentPopup();
	}

	void DrawContents()
	{
		if (EditorWindow::ClosePopupOnEscape())
			return;

		DrawNameRow();
		if (session.features.empty()) {
			BrowserUI::EmptyState(T(TKEY("export.no_features"), "No loaded feature has settings to export."));
			return;
		}

		auto& viewed = session.features[session.viewed];
		LoadFeature(viewed);
		RefreshTarget(viewed);
		DrawDestination(viewed);
		ImGui::Spacing();

		if (!session.featureLocked) {
			DrawFeatureList();
			ImGui::SameLine();
		}
		ImGui::BeginGroup();
		DrawSettings(viewed);
		ImGui::EndGroup();

		ImGui::Spacing();
		const auto counts = CountChanges();
		DrawResults(counts);
		ImGui::Checkbox(T(TKEY("export.remove_unticked"), "Remove unticked settings from the file"), &session.removeUnticked);
		Util::AddTooltip(T(TKEY("export.remove_unticked_tooltip"),
			"Unticked settings the file already holds are deleted from it. Otherwise they are kept as they are."));
		if (session.failed)
			Util::Text::WrappedError("%s", T(TKEY("export.failed"), "Could not export the selected settings. Check the log and try again."));
		ImGui::Spacing();
		DrawButtons(counts);
	}
}

void FeatureOverwritesPanel::BeginExport(Feature* feature)
{
	session = {};
	session.featureLocked = feature != nullptr;
	if (feature) {
		auto& entry = session.features.emplace_back(FeatureExport{ .shortName = feature->GetShortName(), .label = feature->GetDisplayName() });
		LoadFeature(entry);
		std::ranges::fill(entry.ticked, uint8_t{ 1 });
	} else {
		auto features = Feature::GetFeatureList();
		std::ranges::sort(features, {}, [](Feature* candidate) { return candidate->GetDisplayName(); });
		for (auto* candidate : features)
			if (candidate->loaded && HasExportableSettings(candidate))
				session.features.push_back({ .shortName = candidate->GetShortName(), .label = candidate->GetDisplayName() });
	}
	CollectExistingNames();
	session.modal.Request();
}

void FeatureOverwritesPanel::BeginExportInto(Feature* feature, const std::string& packId)
{
	assert(feature);
	BeginExport(feature);
	strncpy_s(target.name, sizeof(target.name), packId.c_str(), _TRUNCATE);
	auto& entry = session.features.front();
	RefreshTarget(entry);
	for (size_t index = 0; index < entry.rows.size(); ++index)
		entry.ticked[index] = entry.rows[index].fileState != FileState::Absent;
}

void FeatureOverwritesPanel::DrawExport()
{
	if (!session.modal.IsActive())
		return;
	const auto title = std::format("{}{}", T(TKEY("export.title"), "Export Feature Settings"), kPopupId);
	session.modal.Draw(title.c_str(), ImGui::GetFontSize() * (session.featureLocked ? kLockedModalWidthEm : kModalWidthEm),
		DrawContents);
}

void FeatureOverwritesPanel::DrawExportButton(Feature* feature, const char* id)
{
	const float size = ImGui::GetFrameHeight();
	{
		auto style = Util::TransparentIconButtonStyle();
		if (Icons::Button(id, Icons::TI(ICON_TI_FILE_EXPORT), ImVec2(size, size)))
			BeginExport(feature);
	}
	const auto tooltip = std::format("{}\n{}", T(TKEY("export.button_title"), "Export Feature Overwrite..."),
		T(TKEY("export.button_tooltip"),
			"Writes this feature's settings into a Baseline preset pack, applied from the Presets page. "
			"Scene layers are left out.\n"
			"For a full preset with scene layers, use Export Preset in the CS Editor (Ctrl+Shift+S)."));
	Util::AddTooltip(tooltip.c_str());
}

#undef I18N_KEY_PREFIX
