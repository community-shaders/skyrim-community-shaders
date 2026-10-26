#pragma once

#include <string>

#include "SceneSettingsManager.h"

/// One header for every feature-settings panel in the editor: the layers a value resolves
/// through, with the one being edited highlighted, and what an edit there does. Base Settings and
/// the scene pages draw the same stack, so it is always clear which layer a panel writes to. Each
/// other layer in it is a link to that layer's page, where one applies here and lists the feature.
namespace SceneLayerHeader
{
	/**
	 * @brief Header for the base layer: the stack with Base highlighted, and the scene layers
	 * overriding this feature right now, each linking to the page that authors it.
	 * @param featureShortName Feature whose base settings the panel below edits; the scene layers in
	 * the stack open on it.
	 */
	void DrawBase(const std::string& featureShortName);

	/**
	 * @brief Header for a scene layer: the stack with the context's layer highlighted.
	 * @param context Context the panel below writes to; its period names the set being edited.
	 * @param featureShortName Feature the panel shows; the other layers in the stack open on it.
	 */
	void DrawScene(const SceneSettingsManager::SceneContextId& context, const std::string& featureShortName);

	/** @brief Every layer of the stack the player is in, what applies on it now and how many settings it overrides across all features; each scene layer links to its page. */
	void DrawSummary();
}
