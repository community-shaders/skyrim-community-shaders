#include "InteriorSun.h"
#include "State.h"

#include <numbers>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	InteriorSun::Settings,
	ForceDoubleSidedRendering,
	InteriorShadowDistance,
	ForceSingleShadowCascade)

void InteriorSun::DrawSettings()
{
	ImGui::Checkbox("Force Double-Sided Rendering", &settings.ForceDoubleSidedRendering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Disables backface culling during sun shadowmap rendering in interiors. "
			"Will prevent most light leaking through unmasked/unprepared interiors at a small performance cost. ");
	}
	ImGui::Checkbox("Force Single Shadow Cascade", &settings.ForceSingleShadowCascade);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Forces the use of a single high-quality shadow cascade for interiors instead of multiple cascades. "
			"Prevents shadow quality degradation at distance, allowing smaller light-blocking masks. "
			"Recommended for properly prepared interior spaces.");
	}
	if (ImGui::SliderFloat("Interior Shadow Distance", &settings.InteriorShadowDistance, 3000.0f, 8000.0f)) {  // min 3000, any less creates visual issues in most interiors.
		*gInteriorShadowDistance = settings.InteriorShadowDistance;
		auto tes = RE::TES::GetSingleton();
		SetShadowDistance(tes && tes->interiorCell);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Sets the distance shadows are rendered at in interiors. "
			"Lower values improve performance but may cause shadow popping and other visual issues.");
	}
}

void InteriorSun::LoadSettings(json& o_json)
{
	settings = o_json;
}

void InteriorSun::SaveSettings(json& o_json)
{
	o_json = settings;
}

void InteriorSun::RestoreDefaultSettings()
{
	settings = {};
}

void InteriorSun::PostPostLoad()
{
	stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

	// Hooks and patch to enable directional lighting for interiors
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x399, 0x37D, 0x639));
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x3AE, 0x392, 0x64E));
	REL::safe_fill(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x397, 0x37B, 0x637), REL::NOP, 2);

	// Hook for overriding the rooms and portals passed to the directional light culling step to fix light leaking through unrendered geometry
	stl::detour_thunk<DirShadowLightCulling>(REL::RelocationID(101498, 108492));

	// Hooks and patches in AIProcess::CalculateLightValue to force interior cells with directional lights to perform raycast checks
	REL::safe_fill(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1E7, 0x1F1), REL::NOP, REL::Module::IsAE() ? 2 : 6);
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1ED, 0x1F3));
	REL::safe_fill(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x2CA, 0x22B), REL::NOP, REL::Module::IsAE() ? 6 : 2);

	gShadowDistance = reinterpret_cast<float*>(REL::RelocationID(528314, 415263).address());
	gInteriorShadowDistance = reinterpret_cast<float*>(REL::RelocationID(513755, 391724).address());

	// Patches BSShadowDirectionalLight::SetFrameCamera to read the correct shadow distance value in interior cells
	const std::uintptr_t address = REL::RelocationID(101499, 108496).address() + REL::Relocate(0xD62, 0xE6C, 0xE72);
	const std::int32_t displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(gShadowDistance) - (address + 8));
	REL::safe_write(address + 4, &displacement, sizeof(displacement));

	// Hook SetFrameCamera to modify shadow split distances for interior sun
	stl::write_vfunc<0x10, BSShadowDirectionalLight_SetFrameCamera>(RE::VTABLE_BSShadowDirectionalLight[0]);

	rasterStateCullMode = globals::game::isVR ? &globals::game::shadowState->GetVRRuntimeData().rasterStateCullMode : &globals::game::shadowState->GetRuntimeData().rasterStateCullMode;

	logger::info("[Interior Sun] Installed hooks");
}

void InteriorSun::EarlyPrepass()
{
	isInteriorWithSun = IsInteriorWithSun(RE::TES::GetSingleton()->interiorCell);
}

inline bool InteriorSun::IsInteriorWithSun(const RE::TESObjectCELL* cell)
{
	return cell && cell->cellFlags.all(RE::TESObjectCELL::Flag::kIsInteriorCell, RE::TESObjectCELL::Flag::kShowSky, RE::TESObjectCELL::Flag::kUseSkyLighting, static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows));
}

RE::TESWorldSpace* InteriorSun::GetWorldSpace::thunk(RE::TES* tes)
{
	if (const auto cell = tes->interiorCell)
		return IsInteriorWithSun(cell) ? enableInteriorSun : disableInteriorSun;
	return func(tes);
}

RE::TESWorldSpace* InteriorSun::enableInteriorSun = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)]{};
	return reinterpret_cast<RE::TESWorldSpace*>(buffer);
}();

RE::TESWorldSpace* InteriorSun::disableInteriorSun = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)] = {};
	const auto noShadows = reinterpret_cast<RE::TESWorldSpace*>(buffer);
	noShadows->flags.set(RE::TESWorldSpace::Flag::kNoSky, RE::TESWorldSpace::Flag::kFixedDimensions);
	return noShadows;
}();

void InteriorSun::DirShadowLightCulling::thunk(RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays, RE::BSTArray<RE::NiPointer<RE::NiAVObject>>& nodes)
{
	auto& singleton = globals::features::interiorSun;
	const auto cell = RE::TES::GetSingleton()->interiorCell;
	auto* passedJobArrays = &jobArrays;

	if (cell && singleton.isInteriorWithSun) {
		const auto* loadedData = cell->GetRuntimeData().loadedData;
		const auto portalGraph = loadedData ? loadedData->portalGraph : nullptr;
		if (portalGraph) {
			singleton.PopulateReplacementJobArrays(cell, portalGraph, dirLight, jobArrays);
			passedJobArrays = &singleton.replacementJobArrays;
		} else
			singleton.currentCell = nullptr;
	} else {
		if (!singleton.arraysCleared)
			singleton.ClearArrays();
		singleton.currentCell = nullptr;
	}

	func(dirLight, *passedJobArrays, nodes);
}

void InteriorSun::BSBatchRenderer_RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	globals::features::interiorSun.UpdateRasterStateCullMode(a_pass, a_technique);
	func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

void InteriorSun::ClearArrays()
{
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

void InteriorSun::PopulateReplacementJobArrays(RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays)
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
		if (addedSet.find(object.get()) != addedSet.end())
			continue;

		// For single cascade mode, include ALL rooms/portals within shadow distance
		// regardless of sun direction to prevent view-dependent culling issues
		if (settings.ForceSingleShadowCascade) {
			addedSet.insert(object.get());
			replacementJobArrays[count++ % jobArraySize].push_back(object);
		} else {
			if (IsInSunDirectionAndWithinShadowDistance(object, lightDir, playerPos)) {
				addedSet.insert(object.get());
				replacementJobArrays[count++ % jobArraySize].push_back(object);
			}
		}
	}

	arraysCleared = false;
}

void InteriorSun::InitialiseOnNewCell(const RE::NiPointer<RE::BSPortalGraph>& portalGraph)
{
	currentCellRoomsAndPortals.clear();

	if (const auto portalSharedNode = portalGraph->portalSharedNode) {
		for (const auto room : portalGraph->rooms)
			currentCellRoomsAndPortals.push_back(room);

		for (auto child : portalGraph->portalSharedNode->GetChildren())
			currentCellRoomsAndPortals.push_back(child);
	}
}

bool InteriorSun::IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const
{
	const float radius = object->worldBound.radius;
	const auto diff = object->worldBound.center - playerPos;
	const float distance = diff.Length();
	const float projection = lightDir.Dot(diff);
	return projection >= -radius && (distance - radius) <= *gShadowDistance;
}

void InteriorSun::SetShadowDistance(bool inInterior)
{
	using func_t = decltype(SetShadowDistance);
	static REL::Relocation<func_t> func{ REL::RelocationID(98978, 105631).address() };
	func(inInterior);
}

bool InteriorSun::BSShadowDirectionalLight_SetFrameCamera::thunk(RE::BSShadowDirectionalLight* a_light, const RE::NiCamera& a_camera)
{
	auto& singleton = globals::features::interiorSun;

	// Save and disable frustum culling for interior sun to prevent view-dependent geometry culling
	bool savedCullingStates = false;
	if (singleton.loaded && singleton.isInteriorWithSun && a_light) {
		auto& runtimeData = a_light->GetShadowDirectionalLightRuntimeData();
		if (runtimeData.cullingProcesses.data()) {
			singleton.savedActivePlanes.clear();
			for (auto& cullingProcessPtr : runtimeData.cullingProcesses) {
				if (cullingProcessPtr) {
					// Save original state
					singleton.savedActivePlanes.push_back(cullingProcessPtr->planes.activePlanes);
					// Disable all frustum planes
					cullingProcessPtr->planes.activePlanes = static_cast<RE::NiFrustumPlanes::ActivePlane>(0);
				} else {
					singleton.savedActivePlanes.push_back(static_cast<RE::NiFrustumPlanes::ActivePlane>(0));
				}
			}
			savedCullingStates = true;
		}
	}

	// Call original function
	bool result = func(a_light, a_camera);

	// Restore original culling process states
	if (savedCullingStates && a_light) {
		auto& runtimeData = a_light->GetShadowDirectionalLightRuntimeData();
		if (runtimeData.cullingProcesses.data() && static_cast<size_t>(runtimeData.cullingProcesses.size()) == singleton.savedActivePlanes.size()) {
			for (std::uint32_t i = 0; i < runtimeData.cullingProcesses.size(); ++i) {
				if (runtimeData.cullingProcesses[i]) {
					runtimeData.cullingProcesses[i]->planes.activePlanes = singleton.savedActivePlanes[i];
				}
			}
		}
		singleton.savedActivePlanes.clear();
	}

	// Override split distances for single cascade mode
	if (result && singleton.loaded && singleton.isInteriorWithSun && singleton.settings.ForceSingleShadowCascade) {
		auto& runtimeData = a_light->GetShadowDirectionalLightRuntimeData();
		const float maxDistance = *singleton.gShadowDistance;

		runtimeData.endSplitDistances[0] = maxDistance;
		runtimeData.endSplitDistances[1] = maxDistance;
		runtimeData.endSplitDistances[2] = maxDistance;

		runtimeData.startSplitDistances[0] = 0.0f;
		runtimeData.startSplitDistances[1] = maxDistance;
		runtimeData.startSplitDistances[2] = maxDistance;
	}

	return result;
}