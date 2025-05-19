#include "InteriorSunShadows.h"
#include "State.h"

#include <numbers>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	InteriorSunShadows::Settings,
	ForceDoubleSidedRendering)

void InteriorSunShadows::DrawSettings()
{
	ImGui::Checkbox("Force Double-Sided Rendering", &settings.ForceDoubleSidedRendering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Forces double-sided vertices during sun shadowmap rendering on interiors. "
			"Will prevent most light leaking through unmasked/unprepared interiors at a small performance cost. ");
	}
}

void InteriorSunShadows::PostPostLoad()
{
	// Hooks and patch to enable directional lighting for interiors
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x399, 0x37D, 0x639));
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x3AE, 0x392, 0x64E));
	REL::safe_fill(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x397, 0x37B, 0x637), 0x90, 2);

	// Hook for overriding the rooms and portals passed to the directional light culling step to fix light leaking through unrendered geometry
	stl::detour_thunk<DirShadowLightCulling>(REL::RelocationID(101498, 108492));

	gShadowDistance = reinterpret_cast<float*>(REL::RelocationID(528314, 415263).address());

	// Patches BSShadowDirectionalLight::SetFrameCamera to read the correct shadow distance value in interior cells
	const std::uintptr_t address = REL::RelocationID(101499, 108496).address() + REL::Relocate(0xD62, 0xE6C, 0xE72);
	const std::int32_t displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(gShadowDistance) - (address + 8));
	REL::safe_write(address + 4, &displacement, sizeof(displacement));

	rasterStateCullMode = globals::game::isVR ? &globals::game::shadowState->GetVRRuntimeData().rasterStateCullMode : &globals::game::shadowState->GetRuntimeData().rasterStateCullMode;

	logger::info("[Interior Sun Shadows] Installed hooks");
}

void InteriorSunShadows::EarlyPrepass()
{
	isInteriorWithSun = IsInteriorWithSun(globals::game::tes->interiorCell);
}

/**
 * @brief Determines if a cell is an interior that supports sun lighting and shadows.
 *
 * Returns true if the given cell is non-null and has the flags for interior cell, show sky, use sky lighting, and sunlight shadows all set.
 *
 * @param cell Pointer to the cell to check.
 * @return true if the cell is an interior with sun lighting and shadows enabled; false otherwise.
 */
inline bool InteriorSunShadows::IsInteriorWithSun(const RE::TESObjectCELL* cell)
{
	return cell && cell->cellFlags.all(RE::TESObjectCELL::Flag::kIsInteriorCell, RE::TESObjectCELL::Flag::kShowSky, RE::TESObjectCELL::Flag::kUseSkyLighting, static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows));
}

/**
 * @brief Returns a dummy TESWorldSpace object based on whether the TES object's interior cell supports sun shadows.
 *
 * If the TES object's interior cell exists and qualifies as an interior with sun (per IsInteriorWithSun), returns a special dummy TESWorldSpace to enable sun shadows. Otherwise, returns a dummy TESWorldSpace that disables sun shadows. Falls back to the original GetWorldSpace function if no interior cell is present.
 *
 * @param tes The TES object whose world space is being queried.
 * @return TESWorldSpace* A pointer to the appropriate dummy TESWorldSpace or the result of the original function.
 */
RE::TESWorldSpace* InteriorSunShadows::GetWorldSpace::thunk(RE::TES* tes)
{
	if (const auto cell = tes->interiorCell)
		return IsInteriorWithSun(cell) ? enableInteriorSunShadows : disableInteriorSunShadows;
	return func(tes);
}

RE::TESWorldSpace* InteriorSunShadows::enableInteriorSunShadows = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)]{};
	return reinterpret_cast<RE::TESWorldSpace*>(buffer);
}();

RE::TESWorldSpace* InteriorSunShadows::disableInteriorSunShadows = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)] = {};
	const auto noShadows = reinterpret_cast<RE::TESWorldSpace*>(buffer);
	noShadows->flags.set(RE::TESWorldSpace::Flag::kNoSky, RE::TESWorldSpace::Flag::kFixedDimensions);
	return noShadows;
}();

void InteriorSunShadows::DirShadowLightCulling::thunk(RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays, RE::BSTArray<RE::NiPointer<RE::NiAVObject>>& nodes)
{
	auto* singleton = GetSingleton();
	const auto cell = globals::game::tes->interiorCell;
	auto* passedJobArrays = &jobArrays;

	if (!cell) {
		singleton->ClearArrays();
	} else {
		const auto portalGraph = cell->GetRuntimeData().loadedData->portalGraph;
		if (singleton->isInteriorWithSun && portalGraph) {
			singleton->PopulateReplacementJobArrays(cell, portalGraph, dirLight, jobArrays);
			passedJobArrays = &singleton->replacementJobArrays;
		}
	}

	func(dirLight, *passedJobArrays, nodes);
}

void InteriorSunShadows::ClearArrays()
{
	if (arraysCleared)
		return;

	currentCellRoomsAndPortals.clear();

	for (auto& jobArray : replacementJobArrays)
		jobArray.clear();

	arraysCleared = true;
}

namespace RE
{
	class BSMultiBoundRoom : public NiNode
	{};
}

void InteriorSunShadows::PopulateReplacementJobArrays(RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays)
{
	if (cell != currentCell) {
		InitialiseOnNewCell(portalGraph);
		currentCell = cell;
	}

	const auto jobArraySize = jobArrays.size();

	if (replacementJobArrays.size() != jobArraySize)
		replacementJobArrays.resize(jobArraySize);

	for (auto& jobArray : replacementJobArrays)
		jobArray.clear();

	addedSet.clear();

	// Copy the original job arrays contents into the replacement job arrays
	uint32_t count = 0;
	for (uint32_t i = 0; i < jobArraySize; ++i) {
		for (const auto& object : jobArrays[i]) {
			replacementJobArrays[i].push_back(object);
			addedSet.insert(object.get());
			count++;
		}
	}

	const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
	auto lightDir = -dirLight->GetShadowDirectionalLightRuntimeData().lightDirection;
	lightDir.Unitize();

	// Add extra rooms and portals that are in the direction of the sun
	for (const auto& object : currentCellRoomsAndPortals) {
		if (addedSet.find(object.get()) != addedSet.end() || !IsInSunDirectionAndWithinShadowDistance(object, lightDir, playerPos))
			continue;

		addedSet.insert(object.get());
		replacementJobArrays[count++ % jobArraySize].push_back(object);
	}

	arraysCleared = false;
}

void InteriorSunShadows::InitialiseOnNewCell(const RE::NiPointer<RE::BSPortalGraph>& portalGraph)
{
	currentCellRoomsAndPortals.clear();

	if (const auto portalSharedNode = portalGraph->portalSharedNode) {
		for (const auto room : portalGraph->rooms)
			currentCellRoomsAndPortals.push_back(room);

		for (auto child : portalGraph->portalSharedNode->GetChildren())
			currentCellRoomsAndPortals.push_back(child);
	}
}

bool InteriorSunShadows::IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const
{
	const float radius = object->worldBound.radius;
	const auto diff = object->worldBound.center - playerPos;
	const float distance = diff.Length();
	const float projection = lightDir.Dot(diff);
	return projection >= -radius && (distance - radius) <= *gShadowDistance;
}