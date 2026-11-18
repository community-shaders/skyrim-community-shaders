#include "ProceduralGrass.h"

#include "DynamicCubemaps.h"
#include "GrassCollision.h"
#include "HiZPyramid.h"
#include "IBL.h"
#include "LightLimitFix.h"
#include "LinearLighting.h"
#include "Skylighting.h"
#include "State.h"
#include "TerrainBlending.h"
#include "TerrainHeightMap.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

#include <numbers>

using namespace PGrassCommon;

namespace
{
	/** @brief Restores the rasterizer, depth-stencil and blend states the grass passes replace. */
	class PipelineStateBackup
	{
	public:
		explicit PipelineStateBackup(ID3D11DeviceContext* a_ctx) :
			ctx(a_ctx)
		{
			ctx->RSGetState(&rasterizer);
			ctx->OMGetDepthStencilState(&depthStencil, &stencilRef);
			ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
		}

		~PipelineStateBackup()
		{
			ctx->RSSetState(rasterizer);
			ctx->OMSetDepthStencilState(depthStencil, stencilRef);
			ctx->OMSetBlendState(blend, blendFactor, sampleMask);
			Util::ReleaseAndNull(rasterizer);
			Util::ReleaseAndNull(depthStencil);
			Util::ReleaseAndNull(blend);
		}

		PipelineStateBackup(const PipelineStateBackup&) = delete;
		PipelineStateBackup& operator=(const PipelineStateBackup&) = delete;

	private:
		ID3D11DeviceContext* ctx;
		ID3D11RasterizerState* rasterizer = nullptr;
		ID3D11DepthStencilState* depthStencil = nullptr;
		UINT stencilRef = 0;
		ID3D11BlendState* blend = nullptr;
		float blendFactor[4]{};
		UINT sampleMask = 0;
	};
}

void ProceduralGrass::PostPostLoad()
{
	// SE 12E3520, 100421 | AE 14CCB30, 107139
	REL::safe_fill(REL::RelocationID(100421, 107139).address() + REL::Relocate(0x523, 0xA3F), REL::NOP, 7);

	// SE 12E3AC0, 100422
	stl::write_thunk_call<Main_RenderShadowmasks_UpdateCamera>(REL::RelocationID(100422, 107140).address() + REL::Relocate(0x7B, 0x69));

	logger::info("[Procedural Grass] Installed hooks");
}

void ProceduralGrass::DataLoaded()
{
	// Turn vanilla grass off before cells load so it never generates; PostDepthRendering keeps it in sync afterwards.
	const auto grassManager = globals::game::grassManager;
	if (settings.Enabled && settings.debugDisableVanillaGrass && grassManager && grassManager->enableGrass && !vanillaGrassSuppressed) {
		vanillaGrassSuppressed = true;
		ConsoleFunc_ToggleGrass();
	}
}

void ProceduralGrass::ClearShaderCache()
{
	grassRendererHighLOD->ClearShaderCache();
	grassRendererMidLOD->ClearShaderCache();
	grassRendererLowLOD->ClearShaderCache();

	Util::ReleaseAndNull(depthClipPS);
	Util::ReleaseAndNull(densityGatherCS);
	Util::ReleaseAndNull(distantAmbientLUTCS);
	Util::ReleaseAndNull(terrainLiftCS);
	terrainLiftOriginValid = false;

	CompileSupportShaders();
}

void ProceduralGrass::Main_RenderShadowmasks_UpdateCamera::thunk(RE::BSGraphics::State* state, RE::NiCamera* camera, bool flag)
{
	func(state, camera, flag);
	globals::features::proceduralGrass.PostDepthRendering();
}

bool ProceduralGrass::ConsoleFunc_ToggleGrass()
{
	using func_t = decltype(&ConsoleFunc_ToggleGrass);
	static REL::Relocation<func_t> func{ REL::RelocationID(22391, 22866) };
	return func();
}

void ProceduralGrass::PostDepthRendering()
{
	const auto ctx = globals::d3d::context;
	const auto renderer = globals::game::renderer;

	// Vanilla grass is turned off only on request and handed back when that request ends.
	if (const auto grassManager = globals::game::grassManager) {
		const bool suppressVanilla = settings.Enabled && settings.debugDisableVanillaGrass;
		if (suppressVanilla && grassManager->enableGrass) {
			ConsoleFunc_ToggleGrass();
			vanillaGrassSuppressed = true;
		} else if (!suppressVanilla && vanillaGrassSuppressed) {
			if (!grassManager->enableGrass)
				ConsoleFunc_ToggleGrass();
			vanillaGrassSuppressed = false;
		}
	}

	const auto player = RE::PlayerCharacter::GetSingleton();

	if (!settings.Enabled || !player || globals::state->isMapMenuOpen) {
		CopyDepthBuffer(ctx, renderer);
		return;
	}

	const PipelineStateBackup stateBackup(ctx);

	GetVisibleQuadrants();
	// Grass Optimizations reuses this shared pyramid later in the frame.
	auto* grassHiZ = globals::hiZPyramid;
	grassHiZ->Build(globals::d3d::device, ctx, true);

	PostDepthRenderPrep(ctx, renderer);
	GenerateBlades(ctx, true);
	RenderDepth(ctx);

	// High and Mid form a dense wall close to the camera; rebuild Hi-Z so Low generation can reject blades behind it.
	if (grassHiZ->HasCurrentSceneDepth() && !grassHiZ->Build(globals::d3d::device, ctx, true)) {
		// An unbound pyramid reads as zero depth and would reject everything.
		grassGlobalsStaging->grassHiZParams = float4::Zero;
		grassGlobalsCB->Update(*grassGlobalsStaging);
	}

	// The terrain lift map measures rendered terrain from this copy; the bound depth target cannot be sampled.
	CopyDepthBuffer(ctx, renderer);
	UpdateTerrainLift(ctx, renderer);

	// Low writes depth in its deferred pass; its PS samples the shadow mask at the blade root instead.
	GenerateBlades(ctx, false);

	// Merge grass depth after terrain blending so grass does not appear transparent over terrain.
	auto& terrainBlending = globals::features::terrainBlending;
	if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
		terrainBlending.MergeSceneDepthIntoBlend();
		ID3D11ShaderResourceView* sceneDepthSRV = Util::GetCurrentSceneDepthSRV(true);
		ctx->PSSetShaderResources(17, 1, &sceneDepthSRV);
	}
}

void ProceduralGrass::CopyDepthBuffer(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer)
{
	const auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	const auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	ID3D11Resource* zPrepassCopyResource;
	ID3D11Resource* mainDepthResource;
	zPrepassCopy.views[0]->GetResource(&zPrepassCopyResource);
	mainDepth.views[0]->GetResource(&mainDepthResource);

	ctx->CopyResource(zPrepassCopyResource, mainDepthResource);

	zPrepassCopyResource->Release();
	mainDepthResource->Release();
}

void ProceduralGrass::UpdateDepthBaseCutoff()
{
	if (depthBlendStrength == settings.grassTerrainBlendStrength && depthBlendHeight == settings.grassTerrainBlendHeight)
		return;

	depthBlendStrength = settings.grassTerrainBlendStrength;
	depthBlendHeight = settings.grassTerrainBlendHeight;
	depthBaseCutoff = -1.0f;
	if (1.0f - depthBlendStrength < 0.999f) {
		// Invert the depth shader's smoothstep once per blend-setting change.
		float lower = 0.0f;
		float upper = 1.0f;
		for (uint32_t iteration = 0; iteration < 24; ++iteration) {
			const float midpoint = (lower + upper) * 0.5f;
			const float opacityRamp = midpoint * midpoint * (3.0f - 2.0f * midpoint);
			const float opacity = 1.0f - (1.0f - opacityRamp) * depthBlendStrength;
			if (opacity < 0.999f)
				lower = midpoint;
			else
				upper = midpoint;
		}
		depthBaseCutoff = upper * std::max(depthBlendHeight, 0.01f);
	}
}

void ProceduralGrass::PostDepthRenderPrep(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer)
{
	// Update the grass collision here, to cover when vanilla grass is disabled
	auto& grassCollision = globals::features::grassCollision;
	if (grassCollision.loaded)
		grassCollision.Update();

	const auto& linearLighting = globals::features::linearLighting;
	const bool prelinearizeTypeColors = linearLighting.IsLinearLightingActive();
	const bool typeColorsACEScg = linearLighting.IsACEScgActive();
	if (resolvedTypeColorsLinear != prelinearizeTypeColors || resolvedTypeColorsACEScg != typeColorsACEScg) {
		resolvedTypeColorsLinear = prelinearizeTypeColors;
		resolvedTypeColorsACEScg = typeColorsACEScg;
		grassTypesDirty = true;
		distantAmbientLUTFrame = UINT32_MAX;
	}

	const float shaderTimer = globals::state->timer;
	float timerDelta = shaderTimer - previousShaderTimer;
	if (timerDelta < 0.0f || timerDelta > 0.1f) {
		timerDelta = 0.0f;
		previousWindDirection = windDirection;
		previousWindSpeed = settings.windSpeed;
	}

	// Resolve frame-uniform TRUE_PBR lighting conversions once.
	constexpr float vanillaPBRLightingScale = 0.65f;
	const float grassLightingScale = prelinearizeTypeColors ? 1.0f : vanillaPBRLightingScale;
	float3 resolvedDirLightColor = float3::Zero;

	if (const auto shaderManager = globals::game::smState) {
		if (const auto shadowSceneNode = shaderManager->shadowSceneNode[0]) {
			const auto sunLight = shadowSceneNode->GetRuntimeData().sunLight;
			if (sunLight) {
				if (const auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(sunLight->light.get())) {
					const auto& lightData = dirLight->GetLightRuntimeData();
					float sunlightScale = 1.0f;
					if (const auto imageSpaceManager = globals::game::imageSpaceManager)
						sunlightScale = imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale;

					const auto lightColor = linearLighting.LightColorToWorking(dirLight);
					const float lightScale = lightData.fade * sunlightScale *
					                         (prelinearizeTypeColors ? linearLighting.settings.directionalLightMult : std::numbers::pi_v<float>);
					resolvedDirLightColor = float3(lightColor.red, lightColor.green, lightColor.blue) * lightScale;
				}
			}
		}
	}
	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC texDesc;
	mainTex.texture->GetDesc(&texDesc);

	const float2 renderSize = Util::ConvertToDynamic(float2((float)texDesc.Width, (float)texDesc.Height));
	SetViewport(ctx, renderSize);

	const auto viewProjMat = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	const auto& row0 = viewProjMat.m[0];
	const auto& row1 = viewProjMat.m[1];
	const auto& cameraPosAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();

	// Keep idle camera motion inside a dead zone, then follow continuously at its edge.
	constexpr float lodOriginDeadZone = 8.0f;
	if (!grassLodOriginInitialized) {
		grassLodOrigin = float2(cameraPosAdjust.x, cameraPosAdjust.y);
		grassLodOriginInitialized = true;
	} else {
		const float dx = cameraPosAdjust.x - grassLodOrigin.x;
		const float dy = cameraPosAdjust.y - grassLodOrigin.y;
		const float distanceSq = dx * dx + dy * dy;
		if (distanceSq > lodOriginDeadZone * lodOriginDeadZone) {
			const float distance = std::sqrt(distanceSq);
			const float follow = (distance - lodOriginDeadZone) / distance;
			grassLodOrigin.x += dx * follow;
			grassLodOrigin.y += dy * follow;
		}
	}

	auto& grassGlobals = *grassGlobalsStaging;
	grassGlobals = GrassGlobals{};
	grassGlobals.cameraViewRow0Sum = abs(row0[0]) + abs(row0[1]) + abs(row0[2]);
	grassGlobals.cameraViewRow1Sum = abs(row1[0]) + abs(row1[1]) + abs(row1[2]);
	const auto clipPlaneExtent = [&](int axis, float sign) {
		return abs(viewProjMat.m[0][3] + sign * viewProjMat.m[0][axis]) +
		       abs(viewProjMat.m[1][3] + sign * viewProjMat.m[1][axis]) +
		       abs(viewProjMat.m[2][3] + sign * viewProjMat.m[2][axis]);
	};
	grassGlobals.frustumPlaneExtent = float4(clipPlaneExtent(0, 1.0f), clipPlaneExtent(0, -1.0f),
		clipPlaneExtent(1, 1.0f), clipPlaneExtent(1, -1.0f));

	// Convert viewport-space SV_Position to normalized coordinates before dynamic-resolution adjustment.
	grassGlobals.dynamicResolutionInverted = float2(1.0f / renderSize.x, 1.0f / renderSize.y);

	grassGlobals.windSpeed = settings.windSpeed;
	grassGlobals.previousWindSpeed = previousWindSpeed;
	grassGlobals.windDir = windDirection;
	grassGlobals.windAngle = atan2(windDirection.y, windDirection.x);
	if (grassGlobals.windAngle < 0.0f)
		grassGlobals.windAngle += 2.0f * std::numbers::pi_v<float>;
	grassGlobals.windRotationScale = settings.windSpeed * settings.windSpeed * settings.windSpeed * 0.5f;
	grassGlobals.previousWindDir = previousWindDirection;
	grassGlobals.grassPBRLightingScale = grassLightingScale;

	// Snap the density window to its texel grid so the map does not shift sub-texel as the camera moves.
	constexpr float densityTexel = DensityHalfExtent * 2.0f / grassDensityDim;
	grassGlobals.occlusionHalfExtent = DensityHalfExtent;
	grassGlobals.occlusionInvExtent = 1.0f / (DensityHalfExtent * 2.0f);
	grassGlobals.occlusionParams = float4(std::floor(cameraPosAdjust.x / densityTexel) * densityTexel, std::floor(cameraPosAdjust.y / densityTexel) * densityTexel, 0.0f, 0.0f);

	grassGlobals.grassAOParams = float4((float)grassDensityDim, 0.0f, 64.0f, settings.grassHeight);
	// Full canopy sky occlusion leaves a sideways-facing surface at the base of dense grass about a third of its sky.
	constexpr float CanopySkyExtinction = 4.0f;
	const float canopySkyExponent = std::clamp(settings.grassCanopySkyOcclusion, 0.0f, 1.0f) * CanopySkyExtinction;
	grassGlobals.grassLightParams = float4(settings.grassDensityAO, canopySkyExponent, 0.0f, settings.grassBaseAO);
	grassGlobals.grassFrameLight = float4(resolvedDirLightColor.x, resolvedDirLightColor.y, resolvedDirLightColor.z, grassLightingScale);

	const auto gridCells = globals::game::tes ? globals::game::tes->gridCells : nullptr;
	grassGlobals.farParams = float4((gridCells ? static_cast<float>(gridCells->length) : 5.0f) * 2048.0f, 0.0f, 0.0f, 0.0f);

	// Terrain LOD is only rendered outside the cells with attached LAND. Empty bounds treat everything as outside.
	float4 loadedLandBounds{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
	if (gridCells) {
		for (uint32_t i = 0; i < gridCells->length * gridCells->length; ++i) {
			const auto* cell = gridCells->cells[i];
			if (!cell || cell->IsInteriorCell() || !cell->IsAttached())
				continue;
			const auto* exterior = cell->GetRuntimeData().cellData.exterior;
			if (!exterior)
				continue;
			const float minX = exterior->cellX * 4096.0f;
			const float minY = exterior->cellY * 4096.0f;
			loadedLandBounds = float4(std::min(loadedLandBounds.x, minX), std::min(loadedLandBounds.y, minY),
				std::max(loadedLandBounds.z, minX + 4096.0f), std::max(loadedLandBounds.w, minY + 4096.0f));
		}
	}
	grassGlobals.loadedLandBounds = loadedLandBounds;

	auto heightMap = globals::terrainHeightMap;
	heightMap->LoadForCurrentWorldspace();

	// Centre the terrain lift window on the camera. A new heightmap means a new worldspace, whose cells share
	// coordinates with the old one, so restart from an origin that maps every texel to a different cell.
	const int32_t liftOriginX = static_cast<int32_t>(std::floor(cameraPosAdjust.x / PGrassCommon::TerrainLiftCellSize)) - PGrassCommon::TerrainLiftDim / 2;
	const int32_t liftOriginY = static_cast<int32_t>(std::floor(cameraPosAdjust.y / PGrassCommon::TerrainLiftCellSize)) - PGrassCommon::TerrainLiftDim / 2;
	const bool reuseTerrainLift = terrainLiftOriginValid && terrainLiftHeightMapGeneration == heightMap->GetLoadGeneration();
	grassGlobals.terrainLiftOrigin[0] = liftOriginX;
	grassGlobals.terrainLiftOrigin[1] = liftOriginY;
	grassGlobals.terrainLiftOrigin[2] = reuseTerrainLift ? terrainLiftOriginCell[0] : liftOriginX + PGrassCommon::TerrainLiftDim * 8;
	grassGlobals.terrainLiftOrigin[3] = reuseTerrainLift ? terrainLiftOriginCell[1] : liftOriginY + PGrassCommon::TerrainLiftDim * 8;
	grassGlobals.terrainLiftPhase = globals::state->frameCount & 3u;

	previousShaderTimer = shaderTimer;
	previousWindDirection = windDirection;
	previousWindSpeed = settings.windSpeed;
	grassGlobals.miscParams = float4(settings.grassMapEdgeNoise, 0.0f, settings.grassViewThicken, timerDelta);
	grassGlobals.grassTerrainBlend = float4(settings.grassTerrainBlendStrength, settings.grassTerrainBlendHeight, settings.grassTerrainBlendNormal, settings.grassTerrainBlendRough);
	UpdateDepthBaseCutoff();

	const auto heightMapScale = heightMap->GetScale();
	grassGlobals.heightMapScale = float2(heightMapScale.x, heightMapScale.y);
	grassGlobals.heightMapOffset = heightMap->GetOffset();
	grassGlobals.heightMapZRange = heightMap->GetPosRange();
	grassGlobals.debugFlags = float2(settings.debugDisableAllCulls ? 1.0f : 0.0f, settings.debugTierView ? 1.0f : 0.0f);

	// Presence-map origin, inverse sample spacing, and dimension.
	grassGlobals.grassPresenceParams = float4(grassPresenceOrigin.x, grassPresenceOrigin.y, (float)(QuadrantGrassPitch - 1) / 2048.0f, (float)grassPresenceDim);
	const auto* grassHiZ = globals::hiZPyramid;
	grassGlobals.grassHiZParams = grassHiZ->HasCurrentSceneDepth() ?
	                                  float4((float)grassHiZ->GetWidth(), (float)grassHiZ->GetHeight(), 0.0f, (float)grassHiZ->GetMipCount()) :
	                                  float4::Zero;
	grassGlobals.grassLodOrigin = grassLodOrigin;

	if (grassTypesDirty)
		ResolveGrassTypes(prelinearizeTypeColors);

	if (grassHiZ->IsValid()) {
		grassGlobals.grassHiZParams.z = std::max(nearHiZRadius, settings.grassHeight);
		// Bound height response, gust strength, and the waveform before tightening depth rejection.
		const float windReach = nearHiZRadius * std::abs(settings.windSpeed) * (1.2f * 1.35f * 1.35f * 0.8f * 0.5f);
		grassGlobals.grassHiZBounds = float4(0.0f, hiZClumpReach, windReach, 0.0f);
	}
	grassGlobals.grassHiZBounds.w = depthBaseCutoff;
	grassGlobalsCB->Update(grassGlobals);

	// The depth prepass reads the same tables as generation.
	ID3D11Buffer* frameBuffers[2] = { *globals::game::perFrame, nullptr };
	ctx->VSSetConstantBuffers(12, 2, frameBuffers);
	ID3D11Buffer* grassBuffers[3] = { grassGlobalsCB->CB(), grassTypesArrayCB->CB(), grassGeneratorTypesCB->CB() };
	ctx->VSSetConstantBuffers(8, 3, grassBuffers);
	ID3D11Buffer* sharedBuffers[2] = { globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	ctx->VSSetConstantBuffers(5, 2, sharedBuffers);

	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

ProceduralGrass::VisibilityOrigin ProceduralGrass::GetVisibilityOrigin()
{
	const auto& position = RE::PlayerCharacter::GetSingleton()->GetPosition();
	return {
		static_cast<int32_t>(std::floor(position.x / 2048.0f)),
		static_cast<int32_t>(std::floor(position.y / 2048.0f)),
		static_cast<int32_t>(std::floor(position.x / 4096.0f)),
		static_cast<int32_t>(std::floor(position.y / 4096.0f))
	};
}

void ProceduralGrass::BindGeneratorState(ID3D11DeviceContext* ctx) const
{
	ID3D11Buffer* sharedBuffers[2] = { globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	ctx->CSSetConstantBuffers(5, 2, sharedBuffers);
	ID3D11Buffer* grassBuffers[3] = { grassGlobalsCB->CB(), grassTypesArrayCB->CB(), grassGeneratorTypesCB->CB() };
	ctx->CSSetConstantBuffers(8, 3, grassBuffers);
	ID3D11Buffer* frameBuffers[2] = { *globals::game::perFrame, nullptr };
	ctx->CSSetConstantBuffers(12, 2, frameBuffers);
	ID3D11ShaderResourceView* heightMapSRV = globals::terrainHeightMap->GetSRV();
	ctx->CSSetShaderResources(0, 1, &heightMapSRV);
	ctx->CSSetSamplers(0, 1, &linearClampSampler);
}

void ProceduralGrass::SetViewport(ID3D11DeviceContext* ctx, const float2 size)
{
	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = size.x;
	vp.Height = size.y;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;

	ctx->RSSetViewports(1, &vp);
}

void ProceduralGrass::GenerateBlades(ID3D11DeviceContext* ctx, const bool nearTiers) const
{
	constexpr float quad = 2048.0f;
	constexpr float invBand = 1.0f / quad;

	constexpr float highToMid = (HighTierQuadrantRadius - 1) * quad;  // High and Mid transition here
	constexpr float midToLow = (MidTierQuadrantRadius - 1) * quad;    // Mid and Low transition here
	constexpr float invMidLowBand = 1.0f / PGrassCommon::MidLowHandoffBand;

	constexpr float gridEdge = LowTierQuadrantRadius * quad;
	// Low dithers out over its last two quadrants.
	constexpr float lowFadeOutStart = gridEdge - 2.0f * quad;
	constexpr float invLowFadeOutBand = 1.0f / (gridEdge - lowFadeOutStart);

	PGrassRendererQuads::GenerateParams params{};
	params.lodOrigin = grassLodOrigin;
	params.disableCulls = settings.debugDisableAllCulls;
	params.edgeNoise = settings.grassMapEdgeNoise;

	BindGeneratorState(ctx);

	if (nearTiers) {
		// Build the canopy-density field before High generation so each emitted blade can cache one density sample.
		if (grassPresenceUploadDirty) {
			ctx->UpdateSubresource(grassPresenceTexture->resource.get(), 0, nullptr, grassPresenceStaging.data(), grassPresenceDim, 0);
			grassPresenceUploadDirty = false;
		}
		ID3D11ShaderResourceView* presSRV = grassPresenceTexture->srv.get();
		ctx->CSSetShaderResources(0, 1, &presSRV);
		ID3D11UnorderedAccessView* densityUAV = grassDensityTexture->uav.get();
		ctx->CSSetUnorderedAccessViews(0, 1, &densityUAV, nullptr);
		ctx->CSSetShader(densityGatherCS, nullptr, 0);
		const uint32_t gatherGroups = (grassDensityDim + 7) / 8;
		ctx->Dispatch(gatherGroups, gatherGroups, 1);

		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ID3D11ShaderResourceView* densitySRV = grassDensityTexture->srv.get();
		ctx->CSSetShaderResources(7, 1, &densitySRV);

		// The density gather uses t0; restore the heightmap for generation.
		ID3D11ShaderResourceView* heightMapSRV = globals::terrainHeightMap->GetSRV();
		ctx->CSSetShaderResources(0, 1, &heightMapSRV);

		// Bounds cover clumping and Low width.
		params.frustumPadding = nearQuadrantFrustumPadding;

		globals::profiler->BeginPass("ProceduralGrass::High Generation");
		params.contentVersion = quadrantsHighVersion;
		params.lodFadeIn = float4(0.0f, 1.0e9f, 0.0f, highToMid + quad);
		params.lodFadeOut = float4(highToMid, invBand, 0.0f, 0.0f);
		grassRendererHighLOD->GenerateBlades(ctx, quadrantsHighLOD, params);
		globals::profiler->EndPass();

		globals::profiler->BeginPass("ProceduralGrass::Mid Generation");
		params.contentVersion = quadrantsMidVersion;
		params.lodFadeIn = float4(highToMid, invBand, 0.0f, midToLow + PGrassCommon::MidLowHandoffBand);
		params.lodFadeOut = float4(midToLow, invMidLowBand, 0.0f, 0.0f);
		grassRendererMidLOD->GenerateBlades(ctx, quadrantsMidLOD, params);
		globals::profiler->EndPass();
		UnbindGeneratorResources(ctx);
		return;
	}

	// An unbound map reads zero, leaving roots on LAND where there is no heightmap to measure against.
	const bool terrainLiftReady = terrainLiftOriginValid && terrainLiftTexture && terrainLiftSurfaceTexture && globals::terrainHeightMap->IsReady();
	ID3D11ShaderResourceView* terrainLiftSRVs[2] = {
		terrainLiftReady ? terrainLiftTexture->srv.get() : nullptr,
		terrainLiftReady ? terrainLiftSurfaceTexture->srv.get() : nullptr
	};
	ctx->CSSetShaderResources(9, ARRAYSIZE(terrainLiftSRVs), terrainLiftSRVs);

	globals::profiler->BeginPass("ProceduralGrass::Low Generation");
	params.contentVersion = quadrantsLowVersion;
	params.frustumPadding = nearQuadrantFrustumPadding;
	params.fadeInPositionPadding = lowFadeInPositionPadding;
	params.lodFadeIn = float4(midToLow, invMidLowBand, 0.0f, gridEdge);
	params.lodFadeOut = float4(lowFadeOutStart, invLowFadeOutBand, 0.0f, 0.0f);
	grassRendererLowLOD->GenerateBlades(ctx, quadrantsLowLOD, params);
	globals::profiler->EndPass();

	UnbindGeneratorResources(ctx);
}

void ProceduralGrass::UnbindGeneratorResources(ID3D11DeviceContext* ctx)
{
	ID3D11UnorderedAccessView* uavs[3] = { nullptr, nullptr, nullptr };
	ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	// Includes Hi-Z at t8 and terrain surface height and weight at t9-t10, before their UAVs are used again.
	ID3D11ShaderResourceView* nullGeneratorSRVs[11]{};
	ctx->CSSetShaderResources(0, ARRAYSIZE(nullGeneratorSRVs), nullGeneratorSRVs);
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->CSSetShaderResources(50, 1, &nullSRV);
	ctx->CSSetShader(nullptr, nullptr, 0);
}

void ProceduralGrass::RenderDepth(ID3D11DeviceContext* ctx) const
{
	const auto& mainDepth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	ctx->OMSetRenderTargets(0, nullptr, mainDepth.views[0]);

	ctx->RSSetState(noCullRS);
	ctx->OMSetDepthStencilState(depthWriteDS, 0);
	ctx->OMSetBlendState(depthOnlyBlend, nullptr, 0xFFFFFFFF);

	ID3D11Buffer* grassCB = grassGlobalsCB->CB();
	const auto generatorTypesCB = grassGeneratorTypesCB->CB();
	ctx->VSSetConstantBuffers(10, 1, &generatorTypesCB);
	ctx->VSSetConstantBuffers(8, 1, &grassCB);
	ctx->PSSetConstantBuffers(8, 1, &grassCB);

	// Screen-space lighting needs near blade depth before the shadow and occlusion passes.
	globals::profiler->BeginPass("ProceduralGrass::High Depth");
	grassRendererHighLOD->RenderDepth(ctx, depthClipPS);
	globals::profiler->EndPass();
	globals::profiler->BeginPass("ProceduralGrass::Mid Depth");
	grassRendererMidLOD->RenderDepth(ctx);
	globals::profiler->EndPass();

	ID3D11ShaderResourceView* nullBladeSRV = nullptr;
	ctx->VSSetShaderResources(0, 1, &nullBladeSRV);
}

void ProceduralGrass::DeferredRendering() const
{
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!settings.Enabled || !player || globals::state->isMapMenuOpen)
		return;

	const auto ctx = globals::d3d::context;
	const auto renderer = globals::game::renderer;

	{
		const PipelineStateBackup stateBackup(ctx);
		DeferredRenderPrep(ctx, renderer);
		RenderGrass(ctx);
	}

	ctx->OMSetRenderTargets(0, nullptr, nullptr);
	MergeLateDepth(ctx, renderer);
}

void ProceduralGrass::DeferredRenderPrep(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer) const
{
	const auto& renderTargets = renderer->GetRuntimeData().renderTargets;
	const auto& mainTex = renderTargets[RE::RENDER_TARGETS::kMAIN];
	const auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	ID3D11RenderTargetView* rtvs[8] = {
		mainTex.RTV,
		renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].RTV,
		renderTargets[RE::RENDER_TARGETS::kRAWINDIRECT_DOWNSCALED].RTV,
		renderTargets[RE::RENDER_TARGETS::kINDIRECT].RTV,
		renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV,
		globals::features::dynamicCubemaps.loaded ? renderTargets[RE::RENDER_TARGETS::kRAWINDIRECT].RTV : nullptr,
		renderTargets[RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS].RTV,
		renderTargets[RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS_DOWNSCALED].RTV,
	};

	D3D11_TEXTURE2D_DESC texDesc;
	mainTex.texture->GetDesc(&texDesc);

	SetViewport(ctx, Util::ConvertToDynamic(float2((float)texDesc.Width, (float)texDesc.Height)));

	ctx->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, mainDepth.views[0]);

	const auto& shadowMask = renderTargets[RE::RENDER_TARGETS::kSHADOW_MASK];
	ctx->PSSetShaderResources(14, 1, &shadowMask.SRV);
	ctx->PSSetSamplers(14, 1, &shadowSampler);

	const auto& precipOcclusionTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];
	ctx->PSSetShaderResources(70, 1, &precipOcclusionTexture.depthSRV);

	ctx->PSSetSamplers(0, 1, &linearClampSampler);

	const auto state = globals::state;
	auto sharedDataCB = state->sharedDataCB->CB();
	auto featureDataCB = state->featureDataCB->CB();
	ctx->PSSetConstantBuffers(5, 1, &sharedDataCB);
	ctx->VSSetConstantBuffers(5, 1, &sharedDataCB);
	ctx->PSSetConstantBuffers(6, 1, &featureDataCB);

	UpdateDistantAmbientLUT(ctx);

	ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
	ctx->PSSetConstantBuffers(12, 1, buffers);
	ctx->VSSetConstantBuffers(12, 1, buffers);

	if (globals::features::lightLimitFix.loaded) {
		auto strictLightDataCB = globals::features::lightLimitFix.strictLightDataCB->CB();
		ctx->PSSetConstantBuffers(3, 1, &strictLightDataCB);
	}

	if (globals::features::skylighting.loaded && globals::features::skylighting.texProbeArray) {
		ID3D11ShaderResourceView* srv = globals::features::skylighting.texProbeArray->srv.get();
		ctx->PSSetShaderResources(50, 1, &srv);
	}

	// Mid colour uses the same geometry table as its depth pass.
	const auto generatorTypesCB = grassGeneratorTypesCB->CB();
	ctx->VSSetConstantBuffers(10, 1, &generatorTypesCB);

	ctx->OMSetDepthStencilState(depthEqualDS, 0);
	ctx->RSSetState(noCullRS);
	ctx->OMSetBlendState(defaultBlend, nullptr, 0xFFFFFFFF);

	ID3D11Buffer* grassBuffers[2] = { grassGlobalsCB->CB(), grassTypesArrayCB->CB() };
	ctx->VSSetConstantBuffers(8, 2, grassBuffers);
	ctx->PSSetConstantBuffers(8, 2, grassBuffers);

	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ProceduralGrass::UpdateDistantAmbientLUT(ID3D11DeviceContext* ctx) const
{
	if (!distantAmbientLUT || !distantAmbientLUTCS)
		return;
	if (distantAmbientLUTFrame == globals::state->frameCount) {
		ID3D11ShaderResourceView* ambientSRV = distantAmbientLUT->srv.get();
		ctx->PSSetShaderResources(73, 1, &ambientSRV);
		return;
	}

	// D3D11 cannot bind one resource for PS reads and CS writes simultaneously.
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->PSSetShaderResources(73, 1, &nullSRV);

	auto* state = globals::state;
	ID3D11Buffer* ambientCBs[2] = { state->sharedDataCB->CB(), state->featureDataCB->CB() };
	ctx->CSSetConstantBuffers(5, 2, ambientCBs);
	ID3D11Buffer* grassCB = grassGlobalsCB->CB();
	ctx->CSSetConstantBuffers(8, 1, &grassCB);

	auto& ibl = globals::features::ibl;
	ID3D11ShaderResourceView* iblSRVs[2] = {
		ibl.loaded && ibl.envIBLTexture ? ibl.envIBLTexture->srv.get() : nullptr,
		ibl.loaded && ibl.skyIBLTexture ? ibl.skyIBLTexture->srv.get() : nullptr,
	};
	ctx->CSSetShaderResources(76, 2, iblSRVs);

	ID3D11UnorderedAccessView* ambientUAV = distantAmbientLUT->uav.get();
	ctx->CSSetUnorderedAccessViews(0, 1, &ambientUAV, nullptr);
	ctx->CSSetShader(distantAmbientLUTCS, nullptr, 0);
	ctx->Dispatch((distantAmbientLUTDim + 7) / 8, (distantAmbientLUTDim + 7) / 8, 1);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	ID3D11ShaderResourceView* nullIBLSRVs[2] = { nullptr, nullptr };
	ctx->CSSetShaderResources(76, 2, nullIBLSRVs);
	ctx->CSSetShader(nullptr, nullptr, 0);

	ID3D11ShaderResourceView* ambientSRV = distantAmbientLUT->srv.get();
	ctx->PSSetShaderResources(73, 1, &ambientSRV);
	distantAmbientLUTFrame = globals::state->frameCount;
}

void ProceduralGrass::UpdateTerrainLift(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer)
{
	terrainLiftOriginValid = false;
	if (!terrainLiftTexture || !terrainLiftMeasuredTexture || !terrainLiftSurfaceTexture || !terrainLiftCS || !globals::terrainHeightMap->IsReady())
		return;

	globals::profiler->BeginPass("ProceduralGrass::Terrain Lift");

	ID3D11Buffer* perFrame = *globals::game::perFrame;
	ctx->CSSetConstantBuffers(12, 1, &perFrame);
	ID3D11Buffer* grassCB = grassGlobalsCB->CB();
	ctx->CSSetConstantBuffers(8, 1, &grassCB);
	ctx->CSSetSamplers(0, 1, &linearClampSampler);

	ID3D11ShaderResourceView* srvs[2] = {
		globals::terrainHeightMap->GetSRV(),
		renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV
	};
	ctx->CSSetShaderResources(0, 2, srvs);
	ID3D11UnorderedAccessView* liftUAVs[3] = { terrainLiftMeasuredTexture->uav.get(), terrainLiftTexture->uav.get(), terrainLiftSurfaceTexture->uav.get() };
	ctx->CSSetUnorderedAccessViews(0, ARRAYSIZE(liftUAVs), liftUAVs, nullptr);
	ctx->CSSetShader(terrainLiftCS, nullptr, 0);
	ctx->Dispatch((PGrassCommon::TerrainLiftDim + 7) / 8, (PGrassCommon::TerrainLiftDim + 7) / 8, 1);
	terrainLiftOriginCell[0] = grassGlobalsStaging->terrainLiftOrigin[0];
	terrainLiftOriginCell[1] = grassGlobalsStaging->terrainLiftOrigin[1];
	terrainLiftHeightMapGeneration = globals::terrainHeightMap->GetLoadGeneration();
	terrainLiftOriginValid = true;

	ID3D11UnorderedAccessView* nullUAVs[3]{};
	ctx->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);
	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	ctx->CSSetShaderResources(0, 2, nullSRVs);
	ctx->CSSetShader(nullptr, nullptr, 0);

	globals::profiler->EndPass();
}

void ProceduralGrass::RenderGrass(ID3D11DeviceContext* ctx) const
{
	globals::profiler->BeginPass("ProceduralGrass::Deferred");

	ID3D11ShaderResourceView* densitySRV = grassDensityTexture->srv.get();
	ctx->PSSetShaderResources(71, 1, &densitySRV);
	ID3D11ShaderResourceView* sceneDepthSRV = Util::GetCurrentSceneDepthSRV(false);
	ctx->PSSetShaderResources(74, 1, &sceneDepthSRV);

	// Only High uses terrain-contact blending; other append buffers render opaque.
	ctx->OMSetBlendState(terrainFadeBlend, nullptr, 0xFFFFFFFF);
	grassRendererHighLOD->RenderGrass(ctx);
	ctx->OMSetBlendState(defaultBlend, nullptr, 0xFFFFFFFF);

	grassRendererMidLOD->RenderGrass(ctx);
	// Low has no depth prepass: its PS is cheap, so one pass with depth writes replaces a second full vertex pass.
	ctx->OMSetDepthStencilState(depthWriteDS, 0);
	grassRendererLowLOD->RenderGrass(ctx);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->VSSetShaderResources(0, 1, &nullSRV);
	ID3D11ShaderResourceView* nullGrassSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
	ctx->PSSetShaderResources(71, 4, nullGrassSRVs);

	globals::profiler->EndPass();
}

void ProceduralGrass::MergeLateDepth(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer) const
{
	// Transparent effects need the depth written by Low, just as the hardware depth test does.
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11ShaderResourceView* previousEffectDepthSRV = nullptr;
	ID3D11ShaderResourceView* previousSceneDepthSRV = nullptr;
	ctx->PSGetShaderResources(3, 1, &previousEffectDepthSRV);
	ctx->PSGetShaderResources(17, 1, &previousSceneDepthSRV);
	ctx->PSSetShaderResources(3, 1, &nullSRV);
	ctx->PSSetShaderResources(17, 1, &nullSRV);

	auto& terrainBlending = globals::features::terrainBlending;
	if (terrainBlending.loaded && terrainBlending.settings.Enabled)
		terrainBlending.MergeSceneDepthIntoBlend();
	else
		CopyDepthBuffer(ctx, renderer);

	ctx->PSSetShaderResources(3, 1, &previousEffectDepthSRV);
	ctx->PSSetShaderResources(17, 1, &previousSceneDepthSRV);
	Util::ReleaseAndNull(previousEffectDepthSRV);
	Util::ReleaseAndNull(previousSceneDepthSRV);
}
