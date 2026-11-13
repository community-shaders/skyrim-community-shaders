#pragma once

#include "CSEditor/UndoHistory.h"

struct Feature;

/** @brief Undo targets for feature settings, shared by the CS Editor and the main menu. */
namespace FeatureUndo
{
	/**
	 * @brief An undo target for a feature's own settings, read and written with the scene layer suspended.
	 * @param feature The feature; it must outlive the history, as every registered feature does.
	 * @return A target keyed "feature:<short name>" and labelled with the feature's name. It captures
	 * nothing while the feature is not loaded, and a restore marks the settings changed.
	 */
	UndoHistory::Target MakeTarget(Feature* feature);
}
