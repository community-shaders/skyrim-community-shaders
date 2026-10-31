#pragma once

#include "SceneSettingsManager.h"

#include "SceneSettingsInternal.h"

/// Resolution of the broadest-to-narrowest location target chain a scene layer keys off.
namespace SceneSettingsLocationTargets
{
	/** @brief Whether a type is one of kLocationTargetTypes. */
	bool IsValidLocationTargetType(SceneSettingsManager::LocationTargetType type);

	/** @brief A form's full name, else its generic form display name. */
	template <class Form>
	std::string GetLocationTargetDisplayName(const Form* form)
	{
		if (const char* fullName = form->GetFullName(); fullName && fullName[0] != '\0')
			return std::string(fullName);
		return Util::GetFormDisplayName(form->GetFormID());
	}

	/// Regions carry no full name, so their editor ID is the only readable label they have.
	std::string GetRegionTargetName(const RE::TESRegion* region);

	/** @brief Whether a keyword names a location type, which by convention starts "LocType". */
	bool IsLocationTypeKeyword(const RE::BGSKeyword* keyword);

	/** @brief Target for a worldspace. */
	SceneSettingsManager::LocationTarget MakeWorldspaceTarget(const RE::TESWorldSpace* worldspace);
	/** @brief Target for a keyword that already passed IsLocationTypeKeyword. */
	SceneSettingsManager::LocationTarget MakeLocationTypeTarget(const RE::BGSKeyword* keyword);
	/** @brief Target for a region; cocCode is the editor ID of the cell that resolved it. */
	SceneSettingsManager::LocationTarget MakeRegionTarget(const RE::TESRegion* region, const std::string& cocCode);
	/** @brief Target for a location; cocCode is the editor ID of the cell that resolved it. */
	SceneSettingsManager::LocationTarget MakeLocationTarget(const RE::BGSLocation* location, const std::string& cocCode);
	/** @brief Target for a cell, whose own editor ID is its coc code. */
	SceneSettingsManager::LocationTarget MakeCellTarget(const RE::TESObjectCELL* cell);

	/// Build the broadest-to-narrowest target chain for a location and the cell that resolved it.
	std::vector<SceneSettingsManager::LocationTarget> BuildLocationTargetChain(
		RE::BGSLocation* location, RE::TESObjectCELL* cell);

	/** @brief Every target the game defines, sorted by type then name, for the editor's picker. */
	std::vector<SceneSettingsManager::LocationTarget> BuildLocationCatalog();

	/** @brief The loaded form a SPID names, or null when it does not parse or resolve. */
	RE::TESForm* ResolveLocationTargetForm(std::string_view formKey);

	/// Resolve the chain an arbitrary target belongs to, preferring the player's own chain when it matches.
	std::vector<SceneSettingsManager::LocationTarget> ResolveLocationTargetChain(
		SceneSettingsManager::LocationTargetType type, std::string_view formKey);

	/// Whether a chain describes an interior, or nothing when no link in it settles the question.
	std::optional<bool> GetLocationChainInteriorState(
		const std::vector<SceneSettingsManager::LocationTarget>& targets);
}
