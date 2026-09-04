#include "ScenePresetExport.h"

#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using SceneContextId = SceneSettingsManager::SceneContextId;

	constexpr const char* kExportPopupId = "##ScenePresetExport";

	/// The list scrolls rather than growing past the screen, like the copy preview does.
	constexpr float kModListHeight = 160.0f;
	constexpr float kModalWidth = 480.0f;

	/// Files a destructive confirmation names before it stops listing and counts the rest.
	constexpr size_t kMaxListedFiles = 8;

	/// GetOverwriteModNames() walks every entry in every scene context, so the list is cached like the
	/// copy source/destination lists next door until the entries it counts change.
	SceneSettingsManager::RevisionCache<std::vector<std::string>> modListCache;

	const std::vector<std::string>& GetCachedModNames(SceneSettingsManager* manager)
	{
		return modListCache.Get(manager->GetEntryPresentationRevision(),
			[manager] { return manager->GetOverwriteModNames(); });
	}

	std::string presetName;
	std::vector<std::filesystem::path> collidingFiles;

	/// The page a pending export belongs to, so only that page's toolbar draws it. Export itself is
	/// global; this only decides which toolbar is responsible for the draw.
	SceneContextId exportContext;
	bool dialogActive = false;
	bool pendingOpen = false;
	bool exportRequested = false;
	Util::ConfirmationPopup exportConfirmation;

	/// Builds the confirmation text for a name that already owns files.
	std::string DescribeCollision(std::string name, const std::vector<std::filesystem::path>& files)
	{
		std::string listed;
		for (size_t index = 0; index < files.size() && index < kMaxListedFiles; ++index)
			listed += std::format("\n  {}", files[index].filename().string());
		if (files.size() > kMaxListedFiles) {
			auto remaining = files.size() - kMaxListedFiles;
			listed += std::vformat(
				T(TKEY("scene_export_replace_more"), "\n  ... and {} more"), std::make_format_args(remaining));
		}

		auto count = files.size();
		return std::vformat(T(TKEY("scene_export_replace_message"),
								 "'{}' already has {} file(s) on disk. Exporting deletes every one of "
								 "them and writes this preset in their place. If another mod owns "
								 "these files, they are gone.{}"),
			std::make_format_args(name, count, listed));
	}

	/// Reports the outcome the same way the copy flow does, through the editor's overlay toast.
	/// Takes the name by value: std::make_format_args needs a non-const lvalue to bind.
	void ReportExportResult(std::string name, bool exported)
	{
		auto message = exported ?
		                   std::vformat(T(TKEY("scene_export_result_success"), "Preset '{}' exported."),
						   std::make_format_args(name)) :
		                   std::vformat(T(TKEY("scene_export_result_failure"),
										   "Preset '{}' export failed. Check the log for details."),
						   std::make_format_args(name));
		EditorWindow::GetSingleton()->ShowNotification(
			message, exported ? Util::Colors::GetSuccess() : Util::Colors::GetError());
	}
}

bool ScenePresetExport::CanExport()
{
	auto* manager = SceneSettingsManager::GetSingleton();
	return manager && (manager->HasAnyUserEntries() || !GetCachedModNames(manager).empty());
}

void ScenePresetExport::Open(const SceneContextId& context)
{
	exportContext = context;
	dialogActive = true;
	pendingOpen = true;
	// A name left over from a cancelled session would arm the destructive path without being retyped.
	presetName.clear();
	collidingFiles.clear();
}

void ScenePresetExport::Draw(const SceneContextId& context)
{
	if (!dialogActive || context != exportContext)
		return;

	auto* manager = SceneSettingsManager::GetSingleton();
	if (!manager) {
		dialogActive = false;
		return;
	}

	if (pendingOpen) {
		ImGui::OpenPopup(kExportPopupId);
		pendingOpen = false;
	}

	ImGui::SetNextWindowSize(ImVec2(kModalWidth * Util::GetUIScale(), 0.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal(kExportPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped(
			"%s", T(TKEY("scene_export_scope"), "Exports every setting from every context, not just this page."));
		ImGui::Separator();

		const auto& modNames = GetCachedModNames(manager);
		if (modNames.empty()) {
			Util::Text::Disabled("%s", T(TKEY("scene_export_no_mods"), "No mods are supplying values."));
		} else {
			ImGui::TextUnformatted(T(TKEY("scene_export_mod_list"), "Mods supplying values, last one wins:"));
			if (ImGui::BeginChild("##ScenePresetExportMods",
					ImVec2(0.0f, kModListHeight * Util::GetUIScale()), ImGuiChildFlags_Borders)) {
				for (size_t index = 0; index < modNames.size(); ++index)
					ImGui::Text("%zu. %s", index + 1, modNames[index].c_str());
			}
			ImGui::EndChild();
		}

		ImGui::Separator();
		ImGui::TextUnformatted(T(TKEY("scene_export_name"), "Preset name"));
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##ScenePresetExportName", &presetName);

		// Non-const: std::make_format_args below needs a non-const lvalue to bind.
		auto sanitizedName = Util::FileHelpers::SanitizeFileName(presetName);
		ImGui::BeginDisabled(sanitizedName.empty());
		if (ImGui::Button(T(TKEY("scene_export_confirm"), "Export"))) {
			collidingFiles = manager->FindPresetFiles(sanitizedName);
			exportConfirmation.title = T(TKEY("scene_export_title"), "Export preset");
			exportConfirmation.message = collidingFiles.empty() ?
			                                 std::vformat(T(TKEY("scene_export_create_message"),
															 "Write your settings out as the preset '{}'?"),
													 std::make_format_args(sanitizedName)) :
			                                 DescribeCollision(sanitizedName, collidingFiles);
			exportConfirmation.confirmLabel = collidingFiles.empty() ?
			                                      T(TKEY("scene_export_confirm"), "Export") :
			                                      T(TKEY("scene_export_replace_confirm"), "Delete and replace");
			exportConfirmation.cancelLabel = T(TKEY("cancel"), "Cancel");
			exportRequested = true;
			exportConfirmation.Request();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("cancel"), "Cancel")))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	if (exportConfirmation.Draw()) {
		auto sanitizedName = Util::FileHelpers::SanitizeFileName(presetName);
		ReportExportResult(sanitizedName, manager->ExportPreset(sanitizedName));
		exportRequested = false;
	} else if (!exportConfirmation.IsOpen()) {
		exportRequested = false;
	}

	if (!ImGui::IsPopupOpen(kExportPopupId) && !exportRequested)
		dialogActive = false;
}

#undef I18N_KEY_PREFIX
