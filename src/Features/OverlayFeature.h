#pragma once

#include "Feature.h"

/**
 * @brief Abstract base class for all features that provide an in-game overlay.
 *
 * Inherit from OverlayFeature if your feature draws an ImGui overlay that can be toggled
 * globally and/or individually. This interface allows the menu system to manage overlays
 * in a generic way.
 */
struct OverlayFeature : Feature
{
	/**
     * @brief Draw the overlay for this feature.
     *
     * This method should render the overlay UI using ImGui. It will only be called if
     * IsOverlayVisible() returns true and the global overlay toggle is enabled.
     */
	virtual void DrawOverlay() = 0;

	/**
     * @brief Whether this overlay should be visible when the global overlay is enabled.
     *
     * Typically, this should return the value of a per-feature setting (e.g., ShowInOverlay).
     * If false, the overlay will not be drawn even if the global overlay is enabled.
     */
	virtual bool IsOverlayVisible() const = 0;

	/**
     * @brief Get the category for UI grouping. Overlays default to "Utility".
     *
     * Subclasses may override this to provide a different category.
     */
	virtual std::string_view GetCategory() const override { return "Utility"; }
};