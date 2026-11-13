#include "SceneSettingsLocationTargets.h"

#include "Globals.h"
#include "Utils/Format.h"

#include <algorithm>
#include <set>

using namespace SceneSettingsInternal;

namespace SceneSettingsLocationTargets
{
	using LocationTargetType = SceneSettingsManager::LocationTargetType;

	constexpr std::string_view kLocationTypePrefix = "LocType";

	bool IsValidLocationTargetType(LocationTargetType type)
	{
		return std::ranges::contains(SceneSettingsManager::kLocationTargetTypes, type);
	}

	std::string GetRegionTargetName(const RE::TESRegion* region)
	{
		auto name = Util::PrettifyIdentifier(Util::GetFormEditorID(region));
		return name.empty() ? Util::GetFormDisplayName(region->GetFormID()) : name;
	}

	bool IsLocationTypeKeyword(const RE::BGSKeyword* keyword)
	{
		return keyword && Util::GetFormEditorID(keyword).starts_with(kLocationTypePrefix);
	}

	std::string GetLocationTypeDisplayName(const RE::BGSKeyword* keyword)
	{
		assert(IsLocationTypeKeyword(keyword));
		auto name = Util::PrettifyIdentifier(Util::GetFormEditorID(keyword).substr(kLocationTypePrefix.size()));
		return name.empty() ? Util::GetFormDisplayName(keyword->GetFormID()) : name;
	}

	SceneSettingsManager::LocationTarget MakeWorldspaceTarget(const RE::TESWorldSpace* worldspace)
	{
		return {
			.type = LocationTargetType::Worldspace,
			.formKey = Util::GetFormFileKey(worldspace),
			.name = GetLocationTargetDisplayName(worldspace),
			.editorId = Util::GetFormEditorID(worldspace),
			.formId = worldspace->GetFormID(),
		};
	}

	SceneSettingsManager::LocationTarget MakeLocationTypeTarget(const RE::BGSKeyword* keyword)
	{
		return {
			.type = LocationTargetType::LocationType,
			.formKey = Util::GetFormFileKey(keyword),
			.name = GetLocationTypeDisplayName(keyword),
			.editorId = Util::GetFormEditorID(keyword),
			.formId = keyword->GetFormID(),
		};
	}

	SceneSettingsManager::LocationTarget MakeRegionTarget(const RE::TESRegion* region, const std::string& cocCode)
	{
		return {
			.type = LocationTargetType::Region,
			.formKey = Util::GetFormFileKey(region),
			.name = GetRegionTargetName(region),
			.editorId = Util::GetFormEditorID(region),
			.cocCode = cocCode,
			.formId = region->GetFormID(),
		};
	}

	SceneSettingsManager::LocationTarget MakeLocationTarget(const RE::BGSLocation* location, const std::string& cocCode)
	{
		return {
			.type = LocationTargetType::Location,
			.formKey = Util::GetFormFileKey(location),
			.name = GetLocationTargetDisplayName(location),
			.editorId = Util::GetFormEditorID(location),
			.cocCode = cocCode,
			.formId = location->GetFormID(),
		};
	}

	SceneSettingsManager::LocationTarget MakeCellTarget(const RE::TESObjectCELL* cell)
	{
		auto cocCode = Util::GetFormEditorID(cell);
		return {
			.type = LocationTargetType::Cell,
			.formKey = Util::GetFormFileKey(cell),
			.name = GetLocationTargetDisplayName(cell),
			.editorId = cocCode,  // The coc code is the cell's own editor ID.
			.cocCode = std::move(cocCode),
			.formId = cell->GetFormID(),
		};
	}

	std::vector<SceneSettingsManager::LocationTarget> BuildLocationTargetChain(
		RE::BGSLocation* location, RE::TESObjectCELL* cell)
	{
		// Away from the player a location still has a place in the world: the cell its marker stands in.
		if (!cell && location && location->worldLocMarker)
			if (auto marker = location->worldLocMarker.get())
				cell = marker->GetParentCell();
		const auto cocCode = cell ? Util::GetFormEditorID(cell) : std::string{};

		std::vector<RE::BGSLocation*> locationChain;
		std::set<RE::FormID> visited;
		for (auto* current = location; current && visited.insert(current->GetFormID()).second; current = current->parentLoc)
			locationChain.push_back(current);
		std::reverse(locationChain.begin(), locationChain.end());

		std::vector<SceneSettingsManager::LocationTarget> targets;
		if (cell && cell->IsExteriorCell())
			if (auto* worldspace = cell->GetRuntimeData().worldSpace)
				targets.push_back(MakeWorldspaceTarget(worldspace));

		// Only the innermost location's types describe where the player stands.
		if (location) {
			std::vector<RE::BGSKeyword*> locationTypes;
			std::set<RE::FormID> seenLocationTypes;
			for (auto* keyword : location->GetKeywords())
				if (IsLocationTypeKeyword(keyword) && seenLocationTypes.insert(keyword->GetFormID()).second)
					locationTypes.push_back(keyword);
			std::ranges::sort(locationTypes, {}, [](const auto* keyword) {
				return NormalizeLocationFormKey(Util::GetFormFileKey(keyword));
			});
			for (auto* locationType : locationTypes)
				targets.push_back(MakeLocationTypeTarget(locationType));
		}

		// The sky knows which of an exterior cell's overlapping regions actually won, but only for the
		// cell the player is standing in; anywhere else the cell's own first region is the best guess.
		if (cell && cell->IsExteriorCell()) {
			RE::TESRegion* region = nullptr;
			if (auto* player = RE::PlayerCharacter::GetSingleton();
				player && player->GetParentCell() == cell && globals::game::sky)
				region = globals::game::sky->region;
			if (!region) {
				if (auto* regions = cell->GetRegionList(false)) {
					auto regionIt = std::find_if(regions->begin(), regions->end(),
						[](auto* candidate) { return candidate != nullptr; });
					if (regionIt != regions->end())
						region = *regionIt;
				}
			}
			if (region)
				targets.push_back(MakeRegionTarget(region, cocCode));
		}

		for (auto* current : locationChain)
			targets.push_back(MakeLocationTarget(current, cocCode));
		if (cell)
			targets.push_back(MakeCellTarget(cell));
		return targets;
	}

	std::vector<SceneSettingsManager::LocationTarget> BuildLocationCatalog()
	{
		std::vector<SceneSettingsManager::LocationTarget> targets;
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return targets;

		for (auto* worldspace : dataHandler->GetFormArray<RE::TESWorldSpace>())
			if (worldspace)
				targets.push_back(MakeWorldspaceTarget(worldspace));
		for (auto* keyword : dataHandler->GetFormArray<RE::BGSKeyword>())
			if (IsLocationTypeKeyword(keyword))
				targets.push_back(MakeLocationTypeTarget(keyword));
		// A region outside any worldspace covers no cells, so it could never apply.
		for (auto* region : dataHandler->GetFormArray<RE::TESRegion>())
			if (region && region->worldSpace)
				targets.push_back(MakeRegionTarget(region, {}));
		for (auto* location : dataHandler->GetFormArray<RE::BGSLocation>())
			if (location)
				targets.push_back(MakeLocationTarget(location, {}));
		// Unnamed cells cannot be told apart in a list, so only cells with an editor ID or a name are offered.
		for (auto* cell : dataHandler->GetFormArray<RE::TESObjectCELL>()) {
			if (!cell)
				continue;
			const char* fullName = cell->GetFullName();
			if (!Util::GetFormEditorID(cell).empty() || (fullName && fullName[0] != '\0'))
				targets.push_back(MakeCellTarget(cell));
		}

		std::erase_if(targets, [](const auto& target) { return target.formKey.empty(); });
		std::ranges::sort(targets, [](const auto& lhs, const auto& rhs) {
			return std::tie(lhs.type, lhs.name, lhs.formKey) < std::tie(rhs.type, rhs.name, rhs.formKey);
		});
		return targets;
	}

	RE::TESForm* ResolveLocationTargetForm(std::string_view formKey)
	{
		const auto parsed = Util::ParseSpid(std::string(formKey));
		if (parsed.localFormId == 0)
			return nullptr;
		const auto formId = parsed.pluginName.empty() ? parsed.localFormId : Util::SpidToFormId(std::string(formKey));
		return formId != 0 ? RE::TESForm::LookupByID(formId) : nullptr;
	}

	std::vector<SceneSettingsManager::LocationTarget> ResolveLocationTargetChain(
		LocationTargetType type, std::string_view formKey)
	{
		// A location type is shared by places everywhere, so the player's chain says nothing about it.
		if (auto* manager = SceneSettingsManager::GetSingleton(); manager && type != LocationTargetType::LocationType) {
			const auto& currentTargets = manager->GetCurrentLocationTargets();
			const auto normalizedKey = NormalizeLocationFormKey(formKey);
			if (std::any_of(currentTargets.begin(), currentTargets.end(), [&](const auto& target) {
					return target.type == type && NormalizeLocationFormKey(target.formKey) == normalizedKey;
				}))
				return currentTargets;
		}
		auto* form = ResolveLocationTargetForm(formKey);
		if (!form)
			return {};
		switch (type) {
		case LocationTargetType::Worldspace:
			if (auto* worldspace = form->As<RE::TESWorldSpace>())
				return { MakeWorldspaceTarget(worldspace) };
			return {};
		case LocationTargetType::LocationType:
			if (auto* keyword = form->As<RE::BGSKeyword>(); IsLocationTypeKeyword(keyword))
				return { MakeLocationTypeTarget(keyword) };
			return {};
		case LocationTargetType::Region:
			// A region is reached through the cells it covers, so away from them only its worldspace bounds it.
			if (auto* region = form->As<RE::TESRegion>()) {
				std::vector<SceneSettingsManager::LocationTarget> targets;
				if (region->worldSpace)
					targets.push_back(MakeWorldspaceTarget(region->worldSpace));
				targets.push_back(MakeRegionTarget(region, {}));
				return targets;
			}
			return {};
		case LocationTargetType::Location:
			return BuildLocationTargetChain(form->As<RE::BGSLocation>(), nullptr);
		case LocationTargetType::Cell:
			if (auto* cell = form->As<RE::TESObjectCELL>())
				return BuildLocationTargetChain(cell->GetLocation(), cell);
			return {};
		default:
			return {};
		}
	}

	std::optional<bool> GetLocationChainInteriorState(
		const std::vector<SceneSettingsManager::LocationTarget>& targets)
	{
		for (const auto& target : targets) {
			// Worldspaces and regions only ever hold exterior cells.
			if (target.type == LocationTargetType::Worldspace || target.type == LocationTargetType::Region)
				return false;
			if (target.type == LocationTargetType::Cell)
				if (auto* form = ResolveLocationTargetForm(target.formKey))
					if (auto* cell = form->As<RE::TESObjectCELL>())
						return cell->IsInteriorCell();
		}
		return std::nullopt;
	}
}
