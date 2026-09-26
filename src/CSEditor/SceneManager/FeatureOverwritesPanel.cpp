#include "FeatureOverwritesPanel.h"

#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsCatalog.h"
#include "Utils/UI.h"

#include <algorithm>
#include <filesystem>
#include <format>

#define I18N_KEY_PREFIX "feature.scene_manager.overwrites."

namespace
{
	constexpr size_t kVisibleExportRows = 12;
	constexpr float kExportPopupWidthEm = 30.0f;
	constexpr const char* kExportComboId = "##FeatureOverwritesExportFeature";

	struct ExportState
	{
		char modName[128]{};
		std::vector<std::string> shortNames;
		std::vector<std::string> labels;
		int featureIndex = -1;
		std::vector<Util::Settings::ExportSetting> settings;
		std::vector<uint8_t> selected;
		ImGuiTextFilter filter;
		bool failed = false;
		bool featureLocked = false;
	};

	ExportState exportState;
	Util::ConfirmationPopup deletePopup;
	std::string deletePath;
	bool actionFailed = false;

	std::string GetExportPopupTitle()
	{
		return std::string(T(TKEY("export.title"), "Export Feature Settings")) + "##FeatureOverwritesExport";
	}

	void SelectFeature(int index)
	{
		exportState.featureIndex = index;
		exportState.settings.clear();
		exportState.filter.Clear();
		exportState.failed = false;
		if (auto* feature = Feature::FindFeatureByShortName(exportState.shortNames[index])) {
			json settings;
			{
				// Export the author's own values, not whatever the active scene is applying.
				SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
				feature->SaveSettings(settings);
			}
			exportState.settings = Util::Settings::GetExportSettings(feature->GetShortName(), settings);
		}
		exportState.selected.assign(exportState.settings.size(), uint8_t{ 1 });
	}

	void AddExportFeature(Feature* feature)
	{
		exportState.shortNames.push_back(feature->GetShortName());
		exportState.labels.push_back(feature->GetDisplayName());
	}

	void AddExportableFeatures()
	{
		auto features = Feature::GetFeatureList();
		std::ranges::sort(features, [](Feature* a, Feature* b) { return a->GetDisplayName() < b->GetDisplayName(); });
		for (auto* feature : features)
			if (feature->loaded && FeatureOverwritesPanel::HasExportableSettings(feature))
				AddExportFeature(feature);
	}

	/** @brief Draws the feature picker; returns true once a feature is selected. */
	bool DrawFeaturePicker()
	{
		ImGui::SetNextItemWidth(-FLT_MIN);
		const char* preview = exportState.featureIndex >= 0 ? exportState.labels[exportState.featureIndex].c_str() :
		                                                      T(TKEY("export.select_feature"), "Select Feature...");
		if (ImGui::BeginCombo(kExportComboId, preview)) {
			const auto searchText = Util::DrawComboSearchInput(kExportComboId);
			for (size_t index = 0; index < exportState.shortNames.size(); ++index) {
				const auto& label = exportState.labels[index];
				if (!searchText.empty() && !Util::StringMatchesSearch(label, searchText))
					continue;
				ImGui::PushID(exportState.shortNames[index].c_str());
				if (ImGui::Selectable(label.c_str(), static_cast<int>(index) == exportState.featureIndex)) {
					SelectFeature(static_cast<int>(index));
					Util::ClearComboSearch(kExportComboId);
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		} else {
			Util::ClearComboSearch(kExportComboId);
		}
		return exportState.featureIndex >= 0;
	}

	void DrawSelectionButtons()
	{
		if (ImGui::Button(T(TKEY("select_all"), "Select All")))
			std::ranges::fill(exportState.selected, uint8_t{ 1 });
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("select_none"), "Select None")))
			std::ranges::fill(exportState.selected, uint8_t{ 0 });
	}

	void DrawSettingList()
	{
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##SettingsFilter", T(TKEY("export.search"), "Search Settings"),
				exportState.filter.InputBuf, IM_ARRAYSIZE(exportState.filter.InputBuf)))
			exportState.filter.Build();

		std::vector<size_t> visible;
		for (size_t index = 0; index < exportState.settings.size(); ++index)
			if (exportState.filter.PassFilter(exportState.settings[index].label.c_str()))
				visible.push_back(index);

		const auto& style = ImGui::GetStyle();
		const float rowHeight = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
		const auto rows = std::clamp(visible.size(), size_t{ 1 }, kVisibleExportRows);
		const float height = rowHeight * static_cast<float>(rows) + style.WindowPadding.y * 2.0f;

		if (ImGui::BeginChild("##Settings", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
			if (ImGui::BeginTable("##SettingList", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(visible.size()), rowHeight);
				while (clipper.Step()) {
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
						const auto index = visible[row];
						const auto& setting = exportState.settings[index];
						ImGui::PushID(setting.path.c_str());
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						bool selected = exportState.selected[index] != 0;
						if (ImGui::Checkbox(setting.label.c_str(), &selected))
							exportState.selected[index] = selected;
						Util::AddTooltip(setting.path.c_str());
						ImGui::PopID();
					}
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
	}

	/** @brief Draws one overwrite row and requests deletion when its button is pressed. */
	void DrawOverwriteRow(const SettingsOverrideManager::OverrideInfo& info)
	{
		ImGui::PushID(info.filePath.c_str());
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		auto* feature = Feature::FindFeatureByShortName(info.featureName);
		const auto name = feature ? feature->GetDisplayName() : std::string(T(TKEY("global"), "Global"));
		ImGui::TextUnformatted(name.c_str());

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(info.modName.c_str());

		ImGui::TableNextColumn();
		const auto filename = std::filesystem::path(info.filePath).filename().string();
		ImGui::TextUnformatted(filename.c_str());
		Util::AddTooltip(info.filePath.c_str());

		ImGui::TableNextColumn();
		if (ImGui::SmallButton(T(TKEY("delete"), "Delete"))) {
			deletePath = info.filePath;
			deletePopup.title = T(TKEY("delete_title"), "Delete Feature Overwrite?");
			deletePopup.message = std::vformat(T(TKEY("delete_message"), "Delete '{0}' from disk? It stops applying on the next load; values already in use are kept."),
				std::make_format_args(filename));
			deletePopup.confirmLabel = T(TKEY("delete"), "Delete");
			deletePopup.cancelLabel = T(TKEY("cancel"), "Cancel");
			deletePopup.Request();
		}

		ImGui::PopID();
	}
}

bool FeatureOverwritesPanel::HasExportableSettings(Feature* feature)
{
	assert(feature);
	// The settings schema is fixed per feature, so one SaveSettings probe is enough.
	static std::unordered_map<Feature*, bool> cache;
	auto [it, inserted] = cache.try_emplace(feature, false);
	if (inserted && feature->UsesMainSettings()) {
		json settings;
		feature->SaveSettings(settings);
		it->second = settings.is_object() && !settings.empty();
	}
	return it->second;
}

void FeatureOverwritesPanel::BeginExport(Feature* feature)
{
	// Reset field by field: ImGuiTextFilter holds ranges pointing into its own buffer, so it must not be copied.
	exportState.shortNames.clear();
	exportState.labels.clear();
	exportState.settings.clear();
	exportState.selected.clear();
	exportState.featureIndex = -1;
	exportState.failed = false;
	exportState.filter.Clear();
	exportState.featureLocked = feature != nullptr;

	if (feature) {
		AddExportFeature(feature);
		SelectFeature(0);
	} else {
		AddExportableFeatures();
	}

	ImGui::OpenPopup(GetExportPopupTitle().c_str());
}

void FeatureOverwritesPanel::DrawExport()
{
	const auto title = GetExportPopupTitle();
	if (!ImGui::IsPopupOpen(title.c_str()))
		return;

	const float width = ImGui::GetFontSize() * kExportPopupWidthEm;
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, ImGui::GetMainViewport()->WorkSize.y));
	bool open = true;
	auto popup = Util::CenteredPopupModal(title.c_str(), &open);
	if (!popup)
		return;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		// The editor ignores Escape only while a popup is open, which this close is about to end.
		if (auto* editor = EditorWindow::GetSingleton(); editor->open)
			editor->suppressNextEditorEscape = true;
		ImGui::CloseCurrentPopup();
		return;
	}

	ImGui::InputText(T(TKEY("export.mod_name"), "Mod Name"), exportState.modName, IM_ARRAYSIZE(exportState.modName));
	const auto modName = Util::FileHelpers::SanitizeFileName(exportState.modName);
	ImGui::TextWrapped("%s", exportState.featureLocked ?
								 T(TKEY("export.description_feature"), "Choose the settings to export, without scene-specific values.") :
								 T(TKEY("export.description"), "Choose one feature and the settings to export, without scene-specific values."));
	ImGui::TextWrapped("%s", T(TKEY("export.existing"), "Existing files with the same name are updated. Other settings and metadata are preserved."));

	if (!exportState.featureLocked && !DrawFeaturePicker())
		return;

	DrawSelectionButtons();
	DrawSettingList();

	if (exportState.failed)
		Util::Text::WrappedError("%s", T(TKEY("export.failed"), "Could not export the selected settings. Check the log and try again."));

	const auto count = std::ranges::count(exportState.selected, uint8_t{ 1 });
	auto disabled = Util::DisableGuard(modName.empty() || count == 0);
	const auto label = std::vformat(T(TKEY("export.count"), "Export ({0})"), std::make_format_args(count));
	if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
		std::vector<std::string> paths;
		for (size_t index = 0; index < exportState.settings.size(); ++index)
			if (exportState.selected[index])
				paths.push_back(exportState.settings[index].path);

		json settings;
		auto* feature = Feature::FindFeatureByShortName(exportState.shortNames[exportState.featureIndex]);
		if (feature) {
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			feature->SaveSettings(settings);
		}

		exportState.failed = !feature || !SettingsOverrideManager::GetSingleton()->ExportSettings(modName,
											 exportState.shortNames[exportState.featureIndex], paths, settings);
		if (!exportState.failed)
			ImGui::CloseCurrentPopup();
	}
}

void FeatureOverwritesPanel::Draw()
{
	auto* manager = SettingsOverrideManager::GetSingleton();

	if (ImGui::Button(T(TKEY("export.button"), "Export Settings")))
		BeginExport();
	if (actionFailed)
		Util::Text::WrappedError("%s", T(TKEY("action_failed"), "Could not update the overwrite. Check the log and try again."));

	bool any = false;
	if (ImGui::BeginTable("##OverwriteFiles", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn(T(TKEY("feature"), "Feature"));
		ImGui::TableSetupColumn(T(TKEY("mod"), "Mod"));
		ImGui::TableSetupColumn(T(TKEY("file"), "File"));
		ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();
		for (const auto& info : manager->GetOverrides()) {
			if (!info.enabled || !manager->IsApplicable(info))
				continue;
			any = true;
			DrawOverwriteRow(info);
		}
		ImGui::EndTable();
	}

	if (!any)
		ImGui::TextDisabled("%s", T(TKEY("empty"), "No feature overwrites are currently applied."));

	if (deletePopup.Draw())
		actionFailed = !manager->DeleteFile(deletePath);

	DrawExport();
}

#undef I18N_KEY_PREFIX
