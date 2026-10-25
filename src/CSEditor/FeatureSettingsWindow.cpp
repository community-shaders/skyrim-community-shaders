#include "FeatureSettingsWindow.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "../I18n/I18n.h"
#include "Browser/BrowserWidgets.h"
#include "EditorWindow.h"
#include "Feature.h"
#include "FeatureCategories.h"
#include "FeatureListPicker.h"
#include "Features/PostProcessing.h"
#include "Globals.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "SaveManager.h"
#include "SceneManager/FeatureOverwritesPanel.h"
#include "SceneManager/SceneLayerHeader.h"
#include "SceneManager/ScenePresetExport.h"
#include "SceneManager/SceneSettingsManager.h"
#include "State.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	/// First-use size as a share of the display; the user can resize afterwards.
	constexpr float kWindowWidthRatio = 0.3f;
	constexpr float kWindowHeightRatio = 0.6f;

	/// Utility and Display hold editor and output plumbing, not scene look. Post Processing is pinned
	/// above the categories instead, as the stack most edits start from.
	constexpr std::array kExcludedCategories = { FeatureCategories::kUtility, FeatureCategories::kDisplay, FeatureCategories::kPostProcessing };

	std::string featureSearch;
	/// Short name of the feature shown in the Features window; also the constraint dialog's navigation target.
	std::string selectedFeature;
	std::map<std::string, bool> categoryExpansion;
	/// Set by Select; the window takes focus on its next Begin.
	bool focusRequested = false;

	/** @brief Centers the next window at its first-use size, re-applied on Reset Window Layout. */
	void SetNextWindowLayout()
	{
		const auto& displaySize = ImGui::GetIO().DisplaySize;
		const auto layoutCond = EditorWindow::GetSingleton()->resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
		ImGui::SetNextWindowSize({ displaySize.x * kWindowWidthRatio, displaySize.y * kWindowHeightRatio }, layoutCond);
		ImGui::SetNextWindowPos({ displaySize.x / 2.0f, displaySize.y / 2.0f }, layoutCond, { 0.5f, 0.5f });
	}

	/// Unlike Feature::FindFeatureByShortName, also finds features that are not loaded.
	Feature* FindAnyFeatureByShortName(const std::string& shortName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && feature->GetShortName() == shortName)
				return feature;
		}
		return nullptr;
	}

	/** @brief An undo target for a feature's own settings, read and written with the scene layer suspended. */
	UndoHistory::Target MakeFeatureUndoTarget(Feature* feature)
	{
		UndoHistory::Target target{ .key = "feature:" + feature->GetShortName(), .label = feature->GetName() };
		target.capture = [feature]() -> UndoHistory::SnapshotPtr {
			if (!feature->loaded)
				return nullptr;
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			nlohmann::json values;
			feature->SaveSettings(values);
			return UndoHistory::Capture(std::move(values));
		};
		target.restore = [feature](const UndoHistory::Snapshot& snapshot) {
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			nlohmann::json values = UndoHistory::Get<nlohmann::json>(snapshot);
			feature->LoadSettings(values);
			globals::state->MarkSettingsChanged();
			globals::state->InvalidateEditsUnderBaselines(feature);
		};
		return target;
	}

	/** @brief Boot enable pill only (label lives in the tooltip). Returns false when not drawn. */
	bool DrawBootToggle(Feature* feature)
	{
		if (!feature || feature->IsAlwaysEnabled())
			return false;

		const auto featureName = feature->GetShortName();
		bool bootEnabled = !globals::state->IsFeatureDisabled(featureName);

		if (Util::FeatureToggle("##BootToggle", &bootEnabled, ImVec2(ImGui::GetFrameHeight() * 1.6f, 0.0f))) {
			const bool nowDisabled = feature->ToggleAtBootSetting();
			logger::info("[CSEditor] {}: {} at boot.", featureName, nowDisabled ? "Disabled" : "Enabled");
		}
		Util::AddTooltip(std::format("{}\n\n{}",
			bootEnabled ?
				T(TKEY("feature_boot_enabled"), "Enabled at boot") :
				T(TKEY("feature_boot_disabled"), "Disabled at boot"),
			T(TKEY("feature_boot_toggle_tooltip"),
				"Toggle whether this feature loads when the game starts.\n"
				"Restart required for changes to take effect.\n"
				"Same as Disable at Boot in the main Community Shaders menu."))
				.c_str());
		return true;
	}

	/** @brief Export overwrite icon, boot controls, and the settings body of one feature. */
	void DrawSettingsPanel(Feature* feature)
	{
		// Names the layer this panel writes to and which scene layers override it here.
		SceneLayerHeader::DrawBase(feature->GetShortName());
		ImGui::Separator();

		const bool canExport = feature->loaded && FeatureOverwritesPanel::HasExportableSettings(feature);
		const float iconSize = ImGui::GetFrameHeight();
		const float rowStartX = ImGui::GetCursorPosX();
		const float rowAvail = ImGui::GetContentRegionAvail().x;

		const bool drewBoot = DrawBootToggle(feature);
		if (canExport) {
			if (drewBoot)
				ImGui::SameLine();
			ImGui::SetCursorPosX(rowStartX + std::max(0.0f, rowAvail - iconSize));
			FeatureOverwritesPanel::DrawExportButton(feature, "##FeatureExportOverwrite");
		}

		if (!feature->loaded) {
			ImGui::Spacing();
			Util::Text::WrappedSecondary("%s",
				T(TKEY("feature_boot_unloaded_hint"),
					"This feature is not loaded. Boot enable/disable still applies after a restart; settings stay hidden until then."));
			if (!feature->failedLoadedMessage.empty()) {
				ImGui::Spacing();
				Util::Text::WrappedError("%s", feature->failedLoadedMessage.c_str());
			}
			return;
		}

		ImGui::Spacing();
		if (ImGui::BeginChild("##FeatureSettingsBody")) {
			auto* editor = EditorWindow::GetSingleton();
			Util::HideDebugSectionsGuard hideDebug(!editor->settings.showFeatureDebug);
			if (feature->UsesMainSettings())
				editor->undoHistory.Watch(MakeFeatureUndoTarget(feature));
			FeatureListRenderer::DrawFeatureBody(feature);
		}
		ImGui::EndChild();

		// Drawn after the body so the modal cannot clobber its hover state.
		if (canExport)
			FeatureOverwritesPanel::DrawExport();
	}

	/// Same dots as the main menu: filled while a scene overrides the feature here, hollow while its
	/// scene settings apply somewhere else.
	FeatureListPicker::Marker SceneMarker(const Feature& feature)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto shortName = const_cast<Feature&>(feature).GetShortName();
		if (!manager || !manager->HasAnySceneEntriesForFeature(shortName))
			return FeatureListPicker::Marker::None;
		return manager->IsFeatureOverridingBase(shortName) ? FeatureListPicker::Marker::Filled : FeatureListPicker::Marker::Hollow;
	}

	const char* SceneMarkerTooltip(const Feature&, FeatureListPicker::Marker marker)
	{
		return marker == FeatureListPicker::Marker::Filled ?
		           T("menu.features.scene_indicator_overriding", "Scene Manager is overriding settings here. Blue settings show its values.") :
		           T("menu.features.scene_indicator_configured", "Has Scene Manager settings for other times, weathers or locations.");
	}

	/** @brief Search bar, Post Processing pinned on top, then collapsible category groups. */
	void DrawFeatureList()
	{
		static const auto features = Feature::GetFeatureList() | std::views::filter([](Feature* feature) {
			return feature->IsInMenu() && !feature->IsHiddenUnreleased() &&
			       std::ranges::find(kExcludedCategories, feature->GetCategory()) == kExcludedCategories.end();
		}) | std::ranges::to<std::vector>();

		auto& postProcessing = globals::features::postProcessing;
		Feature* pinned[] = { &postProcessing };
		const bool pinPostProcessing = postProcessing.IsInMenu() && !postProcessing.IsHiddenUnreleased();
		if (selectedFeature.empty() && pinPostProcessing)
			selectedFeature = postProcessing.GetShortName();

		FeatureListPicker::Draw(features, selectedFeature,
			{
				.search = &featureSearch,
				.categories = &categoryExpansion,
				.pinned = pinPostProcessing ? std::span<Feature* const>(pinned) : std::span<Feature* const>(),
				.marker = SceneMarker,
				.markerTooltip = SceneMarkerTooltip,
			});
	}

	/** @brief Opens the preset export in its Baseline form, on the feature being viewed.
	 *  @param centerY Screen Y to centre the button on, or negative to keep the cursor. */
	void DrawBaselineExportButton(float centerY)
	{
		if (centerY >= 0.0f)
			ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, centerY - BrowserUI::IconButtonSize() * 0.5f));

		const auto tooltip = std::format("{}\n{}", T(TKEY("baseline_export"), "Export Baseline Preset..."),
			T(TKEY("baseline_export_tooltip"),
				"Saves the base settings of the features you tick as a Baseline preset, applied from the Presets page. "
				"Scene layers are left out.\n"
				"For a file loaded at every game start instead, use Export Feature Overwrite beside a feature's settings."));
		if (BrowserUI::IconButton("##BaseSettingsExport", SceneActionIcons::kExport, tooltip.c_str()))
			ScenePresetExport::OpenBaseline(selectedFeature);
	}

	/** @brief The main menu's Save Settings. */
	void DrawSaveSettingsButton()
	{
		ImGui::SameLine();
		// SameLine snaps to the title's line, not the re-centred export button beside it.
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetItemRectMin().y));
		const char* tooltip = T(TKEY("features_save_settings_tooltip"),
			"Save Settings: saves feature settings and everything in the CS editor.");
		if (BrowserUI::IconButton("##FeatureSaveSettings", Icons::TI(ICON_TI_DEVICE_FLOPPY), tooltip))
			SaveManager::SaveAll();
	}

	/** @brief Feature list beside the selected feature's settings. */
	void DrawFeaturesWindow(bool& open)
	{
		SetNextWindowLayout();
		if (std::exchange(focusRequested, false))
			ImGui::SetNextWindowFocus();
		const auto title = std::format("{}###CSEditorFeatures", T(TKEY("base_settings_window"), "Base Settings"));
		const bool visible = Util::BeginWithCustomHeader(title.c_str(), &open, [] {
			// Follows the title, so the previous item is the title text and gives the row's centre.
			ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetStyle().ItemSpacing.x * 2.0f, ImGui::GetCursorScreenPos().y));
			DrawBaselineExportButton((ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
			DrawSaveSettingsButton();
		});
		if (visible) {
			// Docked windows keep the native tab, which has no room for the button, so it leads the body.
			if (ImGui::IsWindowDocked()) {
				DrawBaselineExportButton(-1.0f);
				DrawSaveSettingsButton();
			}

			if (ImGui::BeginTable("##FeaturesLayout", 2, FeatureListPicker::kLayoutFlags)) {
				ImGui::TableSetupColumn("##FeatureList", ImGuiTableColumnFlags_WidthFixed, FeatureListPicker::kColumnWidth * Util::GetUIScale());
				ImGui::TableSetupColumn("##FeatureSettings", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();

				// Each column scrolls on its own, so a long feature list never drags the settings with it.
				ImGui::TableSetColumnIndex(0);
				if (ImGui::BeginChild("##FeatureListColumn"))
					DrawFeatureList();
				ImGui::EndChild();

				// Resolved by name rather than from the list, so constraint navigation to an unlisted feature still shows it.
				ImGui::TableSetColumnIndex(1);
				if (auto* feature = FindAnyFeatureByShortName(selectedFeature))
					DrawSettingsPanel(feature);
				else
					Util::Text::WrappedDisabled("%s", T(TKEY("features_window_select"), "Select a feature on the left."));

				ImGui::EndTable();
			}
		}
		ImGui::End();
	}
}

void FeatureSettingsWindow::Select(const std::string& featureShortName)
{
	selectedFeature = featureShortName;
	focusRequested = true;
	// A search or a collapsed category would hide the row just chosen.
	featureSearch.clear();
	if (auto* feature = FindAnyFeatureByShortName(featureShortName))
		categoryExpansion[std::string(feature->GetCategory())] = true;
}

void FeatureSettingsWindow::Draw(bool& open)
{
	if (!open)
		return;

	DrawFeaturesWindow(open);

	// Drawn at top level so the modal is not clipped by the window; navigating to another feature reopens it.
	const std::string featureBeforeDialog = selectedFeature;
	FeatureListRenderer::DrawConstraintWarningDialog(selectedFeature);
	if (selectedFeature != featureBeforeDialog)
		open = true;
}

#undef I18N_KEY_PREFIX
