#include "CustomSceneControls.h"

#include <imgui.h>

#include "../../I18n/I18n.h"
#include "Features/PostProcessing/LUT.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "SceneSettingsContextRules.h"
#include "SceneSettingsManager.h"
#include "SceneWidgetInterceptor.h"
#include "Utils/UI.h"

namespace
{
	const SceneSettingsManager::SettingIdentity kLutPathSetting{ "PostProcessing", { "LUT", "settings" }, "LutPath" };

	/** @brief Load/Clear acting on one scene context's LUT entry instead of the feature's own LUT. */
	void DrawLutContextControls(LUT& lut, const SceneSettingsManager::SceneContextId& context)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& setting = kLutPathSetting;
		const auto rules = SceneSettingsContextRules::GetSceneContextRules(context);
		const bool allowed = SceneSettingsManager::IsSettingAllowedForType(rules.sceneType, setting.featureShortName,
			setting.settingPath, setting.settingKey, rules.requireNumeric);
		const auto findEntry = [&] { return manager->FindContextUserEntry(context, setting.featureShortName, setting.settingPath, setting.settingKey); };
		const auto isLive = [&](std::optional<size_t> index) {
			const auto entries = manager->GetContextEntries(context);
			return index && *index < entries.size() && !entries[*index].deleted;
		};

		auto entryIndex = findEntry();

		ImGui::BeginGroup();
		ImGui::BeginDisabled(!allowed);
		if (ImGui::Button(T("feature.post_processing.lut.load", "Load"))) {
			lut.errMsg = LUT::ValidateLutFile(UnifiedPresetCatalog::GetSingleton().ResolveActivePackPath(lut.tempPath));
			if (lut.errMsg.empty()) {
				// A tombstoned entry holds no value; clear it so Load's write lands on a live entry.
				if (entryIndex && !isLive(entryIndex)) {
					manager->ClearContextTombstone(context, setting.featureShortName, setting.settingPath, setting.settingKey);
					entryIndex = std::nullopt;
				}
				const auto index = entryIndex ? entryIndex : manager->AddContextSetting(context, setting.featureShortName, setting.settingPath, setting.settingKey, true);
				if (index) {
					const SceneSettingsManager::EntryValueUpdate update{ *index, lut.tempPath };
					manager->UpdateContextEntryValues(context, { &update, 1 });
				}
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!isLive(entryIndex));
		if (ImGui::Button(T("feature.post_processing.lut.clear", "Clear")))
			manager->RemoveContextSetting(context, *entryIndex);
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::EndGroup();
		if (!allowed)
			Util::AddTooltip(T("feature.post_processing.lut.scene_scope_tooltip",
								 "LUTs cannot blend, so they can only be set for Interior Only and location sets without time of day."),
				Util::kTooltipWhenDisabled);
		if (!lut.errMsg.empty()) {
			ImGui::SameLine();
			Util::Text::Error("%s", lut.errMsg.c_str());
		}

		// Remove/Add above can shift indices, so the texture line re-resolves rather than reusing entryIndex.
		entryIndex = findEntry();
		const auto entries = manager->GetContextEntries(context);
		const auto* entry = entryIndex && *entryIndex < entries.size() ? &entries[*entryIndex] : nullptr;
		if (entry && !entry->deleted && entry->value.is_string())
			ImGui::Text(T("feature.post_processing.lut.scene_texture", "Scene Texture: %s"), entry->value.get_ref<const std::string&>().c_str());
		else
			ImGui::TextDisabled("%s", T("feature.post_processing.lut.scene_texture_inherited", "Scene Texture: inherited"));
	}
}

namespace CustomSceneControls
{
	bool DrawLutSceneControls(LUT& lut)
	{
		const auto* scene = SceneWidgetInterceptor::GetArmedContext();
		if (!scene || scene->baseline)
			return false;
		DrawLutContextControls(lut, scene->contextId);
		return true;
	}

	void RecordLutBaselineEdit(const LUT& lut)
	{
		SceneSettingsManager::GetSingleton()->RecordBaselineEdit(kLutPathSetting, lut.settings.LutPath);
	}
}
