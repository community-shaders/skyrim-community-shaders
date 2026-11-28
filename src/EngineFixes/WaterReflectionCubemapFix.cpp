#include "WaterReflectionCubemapFix.h"

#include "Globals.h"
#include "State.h"

namespace
{
	/** @brief Builds a dynamic cubemap reflection the way TESWaterReflections' constructor builds an exterior water's, with no water material. */
	RE::TESWaterReflections* CreateDynamicReflections()
	{
		auto* reflections = RE::malloc<RE::TESWaterReflections>();
		if (!reflections)
			return nullptr;
		std::memset(static_cast<void*>(reflections), 0, sizeof(RE::TESWaterReflections));
		*reinterpret_cast<std::uintptr_t*>(reflections) = RE::VTABLE_TESWaterReflections[0].address();
		stl::atomic_ref objectCount{ *RE::NiRefObject::GetTotalObjectCount() };
		++objectCount;

		reflections->flags.set(RE::TESWaterReflections::Flags::kDynamicCubemap);
		for (std::uint32_t side = 0; side < std::size(reflections->cubeMapSides); ++side)
			reflections->cubeMapSides[side] = { side, 0.0f };
		reflections->IncRefCount();
		return reflections;
	}
}

void WaterReflectionCubemapFix::Install()
{
	stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));
	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);
	stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);
}

void WaterReflectionCubemapFix::ResetCubemapOnSceneChange()
{
	const auto tes = RE::TES::GetSingleton();
	const auto renderer = globals::game::renderer;
	const auto context = globals::d3d::context;
	if (!tes || !renderer || !context)
		return;

	const auto interiorCell = tes->interiorCell;
	const auto worldSpace = interiorCell ? nullptr : tes->GetRuntimeData2().worldSpace;
	static const RE::TESObjectCELL* previousInteriorCell = nullptr;
	static const RE::TESWorldSpace* previousWorldSpace = nullptr;
	static bool initialized = false;
	if (initialized && interiorCell == previousInteriorCell && worldSpace == previousWorldSpace)
		return;

	auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
	for (auto* rtv : reflections.cubeSideRTV)
		if (!rtv)
			return;

	const float clear[4]{};
	for (auto* rtv : reflections.cubeSideRTV)
		context->ClearRenderTargetView(rtv, clear);

	previousInteriorCell = interiorCell;
	previousWorldSpace = worldSpace;
	initialized = true;
}

void WaterReflectionCubemapFix::RenderExteriorCubemap()
{
	// Dynamic Cubemaps and IBL read the cubemap every frame outdoors, but the engine renders it only for dynamic-cubemap water
	const auto sky = globals::game::sky;
	const auto state = globals::state;
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || state->isMapMenuOpen || lastCubemapFrame == state->frameCount)
		return;

	const auto player = RE::PlayerCharacter::GetSingleton();
	const auto waterSystem = RE::TESWaterSystem::GetSingleton();
	if (!player || !player->GetParentCell() || !waterSystem)
		return;

	if (!exteriorReflections)
		exteriorReflections = CreateDynamicReflections();
	if (!exteriorReflections)
		return;

	// TESWaterSystem::Update holds this lock while it updates the water's own reflections
	RE::BSSpinLockGuard guard(waterSystem->lock);
	exteriorReflections->UpdateActor();
}

void WaterReflectionCubemapFix::Main_RenderWaterEffects::thunk()
{
	ResetCubemapOnSceneChange();
	func();
	RenderExteriorCubemap();
}

void WaterReflectionCubemapFix::BSWaterShader_SetupGeometry::thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
{
	func(shader, pass, renderFlags);

	const auto tes = RE::TES::GetSingleton();
	const auto renderer = globals::game::renderer;
	const auto shadowState = globals::game::shadowState;
	if (!tes || !renderer || !shadowState || globals::state->isMapMenuOpen)
		return;

	const auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
	if (!reflections.SRV || shadowState->GetRuntimeData().PSTexture[3] != reflections.SRV)
		return;

	if (!tes->interiorCell) {
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto cell = player ? player->GetParentCell() : nullptr;
		if (!cell || !cell->IsInteriorCell())
			return;
	}

	shadowState->SetPSTexture(3, nullptr);
}

void WaterReflectionCubemapFix::BSCubeMapCamera_RenderCubemap::thunk(RE::NiAVObject* camera, int faces, bool a3, bool a4, bool a5)
{
	lastCubemapFrame = globals::state->frameCount;
	func(camera, faces, a3, a4, a5);
}
