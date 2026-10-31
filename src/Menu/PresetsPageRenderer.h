#pragma once

#include "Presets/UnifiedPresetCatalog.h"

#include <optional>
#include <string>

/** @brief Renders the unified Presets browser for Effects 11 and CS Presets packs. */
class PresetsPageRenderer
{
public:
	/** @brief Draws the page, discovering packs and picking an initial selection on first use. */
	static void Render();

	/** @brief Scans the packs and picks the page's initial selection, once per session; later calls do nothing. */
	static void EnsureDiscovered();

	/** @brief Closes the screenshot lightbox if open. @return True if it consumed the request. */
	static bool CloseLightboxIfOpen();

	/** @brief One colored pill per backend the pack carries. */
	static void DrawBackendBadges(bool hasE11, bool hasCSPresets, bool hasBaseline, bool compact);

	/** @brief Shows the main menu on the Presets page, leaving the CS Editor or Effects 11 editor if one hides it. */
	static void Open();

	/**
	 * @brief Labelled button that opens the Presets page, for feature pages whose presets live there.
	 * @param id Unique ImGui id for the button.
	 * @param tooltip Explains what the page holds for this feature.
	 */
	static void DrawOpenButton(const char* id, const char* tooltip);

	/** @brief Width DrawOpenButton takes, for right-aligning it beside other controls. */
	static float MeasureOpenButton();

	/** @brief Labelled button that opens the CS Editor, right-aligned on the current line. */
	static void DrawOpenCSEditorButton(const char* id);

private:
	/** @brief Search box and backend filter chips. */
	static void RenderToolbar();
	/** @brief The enabled Baseline packs, highest priority first, with arrows to reorder them. Hidden below two packs. */
	static void RenderBaselineOrder();
	/** @brief The filtered pack list. */
	static void RenderList(float width);
	/** @brief The selected pack's artwork, metadata and actions. */
	static void RenderDetail();
	/** @brief The menu themes, each with its CS logo in the theme's accent, while the UI chip is on. */
	static void RenderThemeList(float width);
	/** @brief The selected theme's logo, description, Apply Theme and Open Theme Folder. */
	static void RenderThemeDetail();
	/** @brief Full-viewport screenshot lightbox when a filmstrip thumb is selected. */
	static void RenderScreenshotLightbox();
	/** @brief Forgets the open screenshot and its pack. */
	static void ResetLightbox();
	/** @brief Width DrawBackendBadges will take, for right-aligning it. */
	static float MeasureBackendBadgesWidth(bool hasE11, bool hasCSPresets, bool hasBaseline, bool compact);

	/** @brief The one type shown; null shows all. */
	static std::optional<UnifiedPresetCatalog::PresetType> typeFilter;
	static char searchBuffer[128];
	static std::string selectedPackId;
	static bool discovered;

	/** @brief Pack lightboxImageIndex belongs to, so selecting another pack resets it. */
	static std::string lightboxPackId;
	/** @brief Open lightbox screenshot index into screenshotSRVs; -1 = closed. */
	static int lightboxImageIndex;
	/** @brief Ignores close clicks until the mouse is released after opening. */
	static bool lightboxSuppressClose;
};