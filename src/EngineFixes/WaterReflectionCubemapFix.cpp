#include "WaterReflectionCubemapFix.h"

#include "Globals.h"
#include "State.h"

void WaterReflectionCubemapFix::Install()
{
	stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));
	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);
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

void WaterReflectionCubemapFix::Main_RenderWaterEffects::thunk()
{
	ResetCubemapOnSceneChange();
	func();
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
