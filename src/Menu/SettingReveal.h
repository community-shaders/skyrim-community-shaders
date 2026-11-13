#pragma once

#include "SceneSettingsCatalog.generated.h"

struct Feature;

/**
 * @brief Scrolls to a catalog setting on its feature page and outlines it, after the "Go to..." palette picks it.
 *
 * The request waits for the feature's page to draw. While that body draws under the baseline interceptor scope,
 * the binding of each control reports its catalog entry; the one that matches is scrolled into view and outlined
 * for a moment. A control inside a closed collapsing header is never drawn, so it is never reported: the reveal
 * then shows a "Look under" toast with the setting's path instead. Everything is inert while no request is pending.
 */
namespace SettingReveal
{
	/**
	 * @brief Asks for a setting to be revealed the next time its feature's page draws.
	 * @param setting A catalog entry; it must outlive the request (catalog entries are static).
	 */
	void Request(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief Called before a feature's page body draws; starts looking for the pending control when it is this feature's. */
	void BeginFeatureBody(const Feature* feature);

	/** @brief Called after a feature's page body drew; shows the toast when the control was not drawn, and ends spent requests. */
	void EndFeatureBody();

	/**
	 * @brief Reports a control just drawn by the baseline interceptor; the item rect is the control's.
	 * A match scrolls to it (once) and outlines it while the highlight lasts.
	 */
	void NotifyControl(const SceneSettingsCatalog::SettingMetadata& setting);
}
