#include "PostProcessingMode.h"

#include "Features/Effects11.h"
#include "Features/Effects11/EffectManager.h"
#include "Features/LinearLighting.h"
#include "Features/PostProcessing.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UI.h"

#include <array>
#include <optional>
#include <utility>

namespace PostProcessingMode
{
	namespace
	{
		constexpr int kModeCount = 3;
		constexpr const char* kSettingKey = "Post Processing Mode";
		/// The preset catalog's pipeline revision the saved mode was chosen against.
		constexpr const char* kPackRevisionKey = "Post Processing Mode Pack Revision";
		constexpr Mode kDefaultMode = Mode::Vanilla;

		/** @brief Mode read from the user settings, held until Effects 11 can take it. */
		std::optional<Mode> pendingMode;
		/** @brief The pending mode is only a placeholder, so ApplyPending() picks the default from what is installed. */
		bool detectPreset = false;
		/** @brief The pending mode is the one a pack applied after the last save, not the saved one. */
		bool pendingFromPack = false;
		/** @brief A loaded choice whose pipeline could not run, saved in place of the fallback so it returns with its pipeline. */
		std::optional<Mode> heldMode;
		/** @brief Startup applied a mode, so the pipeline flags reflect a choice and later loads apply right away. */
		bool resolved = false;

		/** @brief Effects 11 is initialized, so its UseEffect setting can be written. */
		bool IsEffects11Ready()
		{
			return globals::features::effects11.loaded && EffectManager::GetSingleton().IsInitialized();
		}

		/** @brief Effects 11 has a compiled preset to run, so it can claim the pipeline slot. */
		bool CanUseEffects11()
		{
			return IsEffects11Ready() && EffectManager::GetSingleton().IsPresetLoaded();
		}

		/** @brief Effects 11 with a loaded preset, else Post Processing if installed, else Vanilla. */
		Mode ResolveDefaultMode()
		{
			if (CanUseEffects11())
				return Mode::Effects11;
			return globals::features::postProcessing.loaded ? Mode::PostProcessing : Mode::Vanilla;
		}

		/** @brief Whether the mode's pipeline can run now; Vanilla always can. */
		bool IsAvailable(Mode mode)
		{
			switch (mode) {
			case Mode::PostProcessing:
				return globals::features::postProcessing.loaded;
			case Mode::Effects11:
				return CanUseEffects11();
			default:
				return true;
			}
		}

		/** @brief Switches the pipeline flags without the user-facing side effects of Set(). */
		void Apply(Mode mode)
		{
			if (IsEffects11Ready())
				globals::features::effects11.SetUseEffect(mode == Mode::Effects11);
			globals::features::postProcessing.bypass = mode != Mode::PostProcessing;
		}
	}

	Mode Get()
	{
		if (globals::features::effects11.IsPresetEnabled())
			return Mode::Effects11;
		const auto& postProcessing = globals::features::postProcessing;
		return postProcessing.loaded && !postProcessing.bypass ? Mode::PostProcessing : Mode::Vanilla;
	}

	bool Set(Mode mode)
	{
		if (!IsAvailable(mode))
			return false;
		pendingMode.reset();
		heldMode.reset();
		resolved = true;
		Apply(mode);
		// Switching to Post Processing always starts linear; running without it is a manual opt-out.
		if (mode == Mode::PostProcessing)
			globals::features::linearLighting.settings.enableLinearLighting = true;
		return true;
	}

	void Load(const json& a_general)
	{
		const auto saved = a_general.find(kSettingKey);
		const auto savedMode = saved != a_general.end() && saved->is_string() ?
		                           magic_enum::enum_cast<Mode>(saved->get<std::string>()) :
		                           std::nullopt;
		// Save() keeps the mode out of the code defaults, so a missing value means the user never saved one.
		detectPreset = !savedMode;
		pendingMode = savedMode.value_or(kDefaultMode);
		pendingFromPack = false;

		// A pack applied after these settings were saved chose the pipeline since, so its choice stands, as it
		// does when there is no saved choice.
		const auto& catalog = UnifiedPresetCatalog::GetSingleton();
		const auto revision = a_general.find(kPackRevisionKey);
		const auto savedRevision = revision != a_general.end() && revision->is_number_unsigned() ? revision->get<uint32_t>() : 0u;
		if (const auto packPipeline = catalog.GetActivePipeline(); packPipeline && (detectPreset || savedRevision != catalog.GetPipelineRevision())) {
			pendingMode = *packPipeline;
			detectPreset = false;
			pendingFromPack = true;
		}
		// Effects 11 is not initialized this early, so only the bypass can apply now.
		globals::features::postProcessing.bypass = *pendingMode != Mode::PostProcessing;
	}

	void Save(json& a_general)
	{
		// Nothing is known until startup applies a mode, which keeps it out of the code defaults; a default still
		// awaiting preset detection stays unsaved so detection runs again on the next boot.
		if (pendingMode ? detectPreset : !resolved)
			return;
		a_general[kSettingKey] = magic_enum::enum_name(pendingMode.value_or(heldMode.value_or(Get())));
		a_general[kPackRevisionKey] = UnifiedPresetCatalog::GetSingleton().GetPipelineRevision();
	}

	bool IsVanillaPending()
	{
		return pendingMode == Mode::Vanilla && !detectPreset;
	}

	void ApplyPending()
	{
		if (!pendingMode)
			return;
		const Mode chosen = detectPreset ? ResolveDefaultMode() : *pendingMode;
		const Mode mode = IsAvailable(chosen) ? chosen : ResolveDefaultMode();
		heldMode = mode != chosen ? std::optional(chosen) : std::nullopt;
		Apply(mode);
		// Set() turns Linear Lighting on with Post Processing; a pack's choice gets the same at startup.
		if (std::exchange(pendingFromPack, false) && mode == Mode::PostProcessing && !heldMode)
			globals::features::linearLighting.settings.enableLinearLighting = true;
		pendingMode.reset();
		resolved = true;
	}

	void ApplyReloaded()
	{
		if (resolved)
			ApplyPending();
	}

	void DrawSelector(const SelectorAction* action)
	{
		std::array<Mode, kModeCount> modes{};
		std::array<const char*, kModeCount> labels{};
		int count = 0;
		int selected = 0;
		const Mode current = Get();
		const auto addSegment = [&](Mode mode, const char* label) {
			if (mode == current)
				selected = count;
			modes[count] = mode;
			labels[count++] = label;
		};

		addSegment(Mode::Vanilla, T("ui.post_processing_mode.vanilla", "Vanilla"));
		if (globals::features::postProcessing.loaded)
			addSegment(Mode::PostProcessing, T("ui.post_processing_mode.post_processing", "Post Processing"));
		if (CanUseEffects11())
			addSegment(Mode::Effects11, T("ui.post_processing_mode.effects11", "Effects 11"));

		const float rowTop = ImGui::GetCursorPosY();
		// Re-clicking the active mode must not undo a manual Linear Lighting opt-out.
		if (Util::SegmentedControl("PostProcessingMode", labels.data(), count, selected) && modes[selected] != current) {
			Set(modes[selected]);
			// Every pipeline change reloads the shader cache.
			globals::shaderCache->SetRecachePending(true);
			// The segmented control does not flag an ImGui edit, so the overlay's check never sees this change.
			globals::state->CheckCoreSettingsChanged();
			// Set() may have turned Linear Lighting on, which the host page's own feature check does not cover.
			globals::state->CheckFeatureSettingsChanged(&globals::features::linearLighting);
		}

		Util::AddTooltip(T("ui.post_processing_mode.tooltip",
			"Only one pipeline runs at a time. Vanilla uses the game's original post processing."));

		if (!action || !action->label)
			return;

		// The page's action sits on the selector's row, flush right and as tall as the selector
		const float rowBottom = ImGui::GetCursorPosY();
		const float stripHeight = rowBottom - rowTop - ImGui::GetStyle().ItemSpacing.y;
		const float buttonWidth = ImGui::CalcTextSize(action->label).x + ImGui::GetStyle().FramePadding.x * 4.0f;
		const float right = ImGui::GetContentRegionMax().x;
		ImGui::SetCursorPos(ImVec2(right - buttonWidth, rowTop));
		if (Util::Button(action->label, ImVec2(buttonWidth, stripHeight)) && action->onClick)
			action->onClick();
		if (action->tooltip)
			Util::AddTooltip(action->tooltip);
		ImGui::SetCursorPosY(rowBottom);
	}
}
