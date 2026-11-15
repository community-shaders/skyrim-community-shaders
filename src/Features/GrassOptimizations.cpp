#include "GrassOptimizations.h"

#include "Profiler.h"
#include "State.h"
#include "TruePBR.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "Utils/VersionedRelocation.h"

#define I18N_KEY_PREFIX "feature.grass_optimizations."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassOptimizations::Settings,
	MinPixelSize,
	FullDetailPixelSize,
	MinDensity,
	MeshCostBias,
	CostBiasStartDistance,
	InvisibleFadeCull,
	RenderDistanceOverride,
	EdgeFadeStart,
	EnableOcclusionCulling,
	OcclusionDepthBias,
	OcclusionTightness,
	SimpleShadingPixelSize,
	CollisionDistance,
	EnableMeshLOD,
	EnableMidLOD,
	MidLODPixelSize,
	EnableFarLOD,
	FarLODPixelSize,
	MeshLODBandPixels)

void GrassOptimizations::LoadSettings(json& o_json)
{
	settings = o_json;
}

void GrassOptimizations::SaveSettings(json& o_json)
{
	o_json = settings;
}

void GrassOptimizations::RestoreDefaultSettings()
{
	settings = {};
}

void GrassOptimizations::DrawSettings()
{
	ImGui::SeparatorText(T(TKEY("culling"), "Culling & LOD"));

	ImGui::SliderFloat(T(TKEY("full_detail_pixel_size"), "Full-Detail Pixel Size"), &settings.FullDetailPixelSize, 4.0f, 128.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("full_detail_pixel_size_tooltip"),
							  "Instances whose on-screen radius is at or above this render at full density. Below it, density decreases toward Minimum Density as instances get smaller on screen. Increasing this setting starts thinning closer grass."));
	}

	ImGui::SliderFloat(T(TKEY("min_pixel_size"), "Min Pixel Size"), &settings.MinPixelSize, 1.0f, 32.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("min_pixel_size_tooltip"),
							  "Individual grass instances that visibly take up less space on the screen than this are dropped entirely. Higher values improve performance by removing far-away grass instances sooner."));
	}

	Util::PercentageSlider(T(TKEY("min_density"), "Minimum Density"), &settings.MinDensity);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("min_density_tooltip"),
							  "The lowest density targeted by thinning. Min Pixel Size and other culling tests can still remove instances."));
	}

	Util::PercentageSlider(T(TKEY("mesh_cost_bias"), "Mesh Cost Bias"), &settings.MeshCostBias);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("mesh_cost_bias_tooltip"),
							  "Culls or removes grass meshes based on their complexity (performance impact). At 0, removal is identical between all grass types regardless of complexity. At 1, heavier and more complex meshes are culled 2-6x sooner than simple ones. Only applies beyond the Cost Bias Start Distance, so nearby grass is never thinned."));
	}

	ImGui::SliderFloat(T(TKEY("cost_bias_start_distance"), "Cost Bias Start Distance"), &settings.CostBiasStartDistance, 0.0f, 20000.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::vector<std::string> tooltipLines = {
			T(TKEY("cost_bias_start_distance_tooltip"),
				"Distance at which Mesh Cost Bias starts taking effect, ramping to full over the same distance again. Nearer than this, all grass types are treated identically no matter how complex. Zero applies the bias everywhere, including right in front of the player."),
			Util::Units::FormatDistance(settings.CostBiasStartDistance)
		};
		Util::DrawMultiLineTooltip(tooltipLines);
	}

	ImGui::SliderFloat(T(TKEY("render_distance_override"), "Grass Render Distance"), &settings.RenderDistanceOverride, 0.0f, 100000.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("render_distance_override_tooltip"),
							  "Max grass render distance in units. 0 = use the game's INI cap (fGrassStartFadeDistance + fGrassFadeRange). Any grass beyond the vanilla range or this range will be removed."));
	}

	Util::PercentageSlider(T(TKEY("edge_fade_start"), "Edge Fade Start"), &settings.EdgeFadeStart);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::vector<std::string> tooltipLines = {
			T(TKEY("edge_fade_start_tooltip"),
				"Percent of the grass render distance at which grass starts fading out. The default of 0.85 fades over the last 15%. A lower value results in a longer, smoother fade out, while a value of 1.0 disables the fade and grass pops out at the render distance."),
			Util::Units::FormatDistance(maxGrassDistance * settings.EdgeFadeStart)
		};
		Util::DrawMultiLineTooltip(tooltipLines);
	}

	ImGui::SliderFloat(T(TKEY("invisible_fade_cull"), "Invisible Fade Cull"), &settings.InvisibleFadeCull, 0.0f, 0.5f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("invisible_fade_cull_tooltip"),
							  "Skip drawing grass whose transparency is below this threshold. Grass with a fade value of zero is completely invisible and thus is removed early for performance reasons."));
	}

	ImGui::SliderFloat(T(TKEY("simple_shading_px"), "Simple Shading Below"), &settings.SimpleShadingPixelSize, 0.0f, 32.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("simple_shading_px_tooltip"),
							  "Grass instances smaller than this size on screen will skip barely visible detail including contact shadows, specular highlights, and other complex grass visual elements. Zero disables this feature."));
	}

	ImGui::SliderFloat(T(TKEY("collision_distance"), "Collision Distance"), &settings.CollisionDistance, 0.0f, 8192.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::vector<std::string> tooltipLines = {
			T(TKEY("collision_distance_tooltip"),
				"Grass beyond this distance skips any collision detection. Zero disables collision on all grass. Requires the Grass Collision feature."),
			Util::Units::FormatDistance(settings.CollisionDistance)
		};
		Util::DrawMultiLineTooltip(tooltipLines);
	}

	ImGui::Spacing();

	ImGui::Checkbox(T(TKEY("occlusion_culling"), "Occlusion Culling"), &settings.EnableOcclusionCulling);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("occlusion_culling_tooltip"),
							  "Skips grass hidden behind rocks, buildings and NPCs. Depending on how much grass is not visible, this may cost more than its benefits. If you see grass flickering when moving, try disabling this. Terrain such as hills is not treated as an occluder when Terrain Blending is enabled, which reduces the benefit."));
	}

	Util::PercentageSlider(T(TKEY("occlusion_bias"), "Occlusion Bias"), &settings.OcclusionDepthBias, 0.0f, 20.0f);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("occlusion_bias_tooltip"),
							  "How far behind an occluder grass must sit before Occlusion Culling removes it, as a percentage of the occluder's distance from the camera, so it behaves the same near and far. Raise this if grass disappears around the edges of rocks and hills, lower it to reclaim more performance. Has no effect unless Occlusion Culling is enabled."));
	}

	ImGui::SliderFloat(T(TKEY("occlusion_tightness"), "Occlusion Tightness"), &settings.OcclusionTightness, 0.25f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("occlusion_tightness_tooltip"),
							  "How small each grass instance is treated as when testing whether it is hidden. At 1.0 grass is culled only once its nearest point is hidden. Lower values shrink both its footprint and that test depth, culling more grass behind occluders for extra performance at the cost of more popping along the edges of rocks and hills. Only affects per-instance culling, never whole patches, and has no effect unless Occlusion Culling is enabled."));
	}

	ImGui::SeparatorText(T(TKEY("mesh_lod"), "Mesh LOD"));

	ImGui::Checkbox(T(TKEY("enable_mesh_lod"), "Enable Mesh LOD"), &settings.EnableMeshLOD);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_mesh_lod_tooltip"),
							  "Improves performance by swapping distant grass instances for a simpler LOD mesh, in two bands. Requires an LOD .nif per grass type at meshes\\LOD\\Grass\\<source-mesh-name>_LOD0.nif, plus an optional _LOD1.nif for the far band. Grass with no LOD mesh keeps its full mesh."));
	}

	ImGui::BeginDisabled(!settings.EnableMeshLOD);

	ImGui::Checkbox(T(TKEY("enable_mid_lod"), "Enable Middle LOD"), &settings.EnableMidLOD);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_mid_lod_tooltip"),
							  "Swaps mid-distance grass to the _LOD0.nif mesh. With this off, grass stays on its full mesh until the far band takes over."));
	}

	ImGui::SliderFloat(T(TKEY("mid_lod_pixel_size"), "Middle LOD Pixel Size"), &settings.MidLODPixelSize, 1.0f, 64.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("mid_lod_pixel_size_tooltip"),
							  "Instances whose on-screen radius is below this but above the Far LOD Pixel Size swap to the _LOD0.nif mesh."));
	}

	ImGui::Checkbox(T(TKEY("enable_far_lod"), "Enable Far LOD"), &settings.EnableFarLOD);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_far_lod_tooltip"),
							  "Swaps the most distant grass to the _LOD1.nif mesh. Grass types without that file reuse their _LOD0.nif, so the far band still gets its own brightness."));
	}

	ImGui::SliderFloat(T(TKEY("far_lod_pixel_size"), "Far LOD Pixel Size"), &settings.FarLODPixelSize, 1.0f, 64.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("far_lod_pixel_size_tooltip"),
							  "Instances whose on-screen radius is below this but above Min Pixel Size swap to the _LOD1.nif mesh. Values above the Middle LOD Pixel Size are clamped to it, since the far band is always the more distant of the two."));
	}

	ImGui::SliderFloat(T(TKEY("mesh_lod_band"), "Mesh LOD Transition Band"), &settings.MeshLODBandPixels, 0.0f, 16.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("mesh_lod_band_tooltip"),
							  "The range of on-screen sizes over which a random amount of meshes are swapped out before completely transitioning to the next LOD. Applies to both transitions. A wider range results in a smoother transition."));
	}

	ImGui::EndDisabled();
}

bool GrassOptimizations::HasShaderDefine(RE::BSShader::Type shaderType)
{
	return shaderType == RE::BSShader::Type::Grass;
}

void GrassOptimizations::PostPostLoad()
{
	Hooks::Install();
}

void GrassOptimizations::Reset()
{
	EndDrawPass();
	bucketStore.EndFrame();
	bucketStore.meshLibrary.CommitLoadedLODMeshes();
}

void GrassOptimizations::ClearShaderCache()
{
	cullCS = nullptr;
	cullCSFailed = false;
	bucketStore.meshLibrary.ClearShaderCache();
}

RE::NiDirectionalLight* GrassOptimizations::GetSunLight()
{
	auto* sceneNode = globals::game::smState->shadowSceneNode[0];
	auto* sun = sceneNode ? sceneNode->GetRuntimeData().sunLight : nullptr;
	return sun ? skyrim_cast<RE::NiDirectionalLight*>(sun->light.get()) : nullptr;
}

void GrassOptimizations::UpdateGrass(const RE::NiCamera* cam)
{
	auto* ctx = globals::d3d::context;

	if (!OwnsGrass())
		return;

	if (!GetCullCS()) {
		ownsGrass.store(false, std::memory_order_release);
		DisableOptimizedDraws();
		return;
	}

	if (auto* grassManager = RE::BGSGrassManager::GetSingleton())
		Hooks::NiNode_OnVisible::grassRoot.store(grassManager->grassNode.get(), std::memory_order_relaxed);

	timeAccum += globals::game::smState->timerValues[1];
	prevTimeBase = timeBase;
	timeBase = globals::game::smState->timerValues[4] * 0.0016666667f * 6.2831802f;

	const auto iniFloat = [](const char* name, float fallback) {
		auto* setting = RE::GetINISetting(name);
		return setting ? setting->GetFloat() : fallback;
	};

	static auto* fadeInSetting = RE::GetINISetting("fGrassFadeInTime:Grass");
	const float fadeInTime = fadeInSetting ? fadeInSetting->GetFloat() : 0.0f;
	fadeInTimeRcp = fadeInTime > 0.0f ? 1.0f / fadeInTime : 1e6f;

	if (vanillaMaxDistance == 0.0f) {
		grassStartFadeDistance = iniFloat("fGrassStartFadeDistance:Grass", 6000.0f);
		vanillaMaxDistance = grassStartFadeDistance + iniFloat("fGrassFadeRange:Grass", 2000.0f);
	}

	maxGrassDistance = settings.RenderDistanceOverride > 0.0f ? settings.RenderDistanceOverride : vanillaMaxDistance;
	maxDistSq = maxGrassDistance * maxGrassDistance;

	// Start fading one frame early to avoid gaps when grass is replaced.
	bucketStore.BeginFrame({ settings.EnableMeshLOD, settings.EnableMidLOD, settings.EnableFarLOD, timeAccum - globals::game::smState->timerValues[1] });

	globals::profiler->BeginPass("GrassOptimizations::UploadPending");
	bucketStore.UploadPending(ctx);
	globals::profiler->EndPass();

	if (!cam) {
		DisableOptimizedDraws();
		return;
	}

	CullGrass(cam);
}

GrassOptimizations::GrassPassSetup GrassOptimizations::SetupGrassPass(RE::BSShader* shader, RE::BSRenderPass* pass)
{
	const auto* snapshot = ActiveSnapshot();
	if (const auto* group = snapshot ? snapshot->FindGroup(pass->geometry) : nullptr) {
		// Set up the first drawable bucket here. DrawInstanceTriShape handles the rest.
		const auto first = std::ranges::find_if(group->buckets, [this](const GrassBucket* bucket) { return IsBucketDrawable(*bucket); });
		if (first == group->buckets.end())
			return GrassPassSetup::kCovered;

		SetupOptimizedPass(**first);

		return GrassPassSetup::kOptimized;
	}

	// Restore material setup for draws that bypass optimized buckets.
	const auto* shaderProperty = pass->shaderProperty;
	if (loaded && shaderProperty && shaderProperty->material)
		Hooks::BSGrassShader_SetupMaterial::func(shader, static_cast<const RE::BSLightingShaderMaterialBase*>(shaderProperty->material));

	return GrassPassSetup::kNative;
}

void GrassOptimizations::SetupOptimizedPass(const GrassBucket& b)
{
	ID3D11Buffer* bucketCB = cullBucketCB->CB();
	UINT windowFirst = (b.cullSlot * kSlotBytes + kSlotMaterialOffset) / 16;
	UINT geometryNum = 32;

	ctx1->VSSetConstantBuffers1(2, 1, &bucketCB, &windowFirst, &geometryNum);
	ctx1->PSSetConstantBuffers1(2, 1, &bucketCB, &windowFirst, &geometryNum);

	// SetupGeometry applies sampler modes after the hook chain returns.
	globals::game::shadowState->SetPSTexture(0, b.drawResources.diffuseTexture->rendererTexture);

	// Canopy heights use bucket constants, while shader flags stay shared.
	globals::state->SetGrassFlags(b.drawResources.key.draw.sphereNormal, b.drawResources.key.draw.pbrMaterial);

	if (b.drawResources.key.draw.pbrMaterial) {
		TruePBR::SetGrassPBRTextures(static_cast<const BSLightingShaderMaterialPBR*>(b.drawResources.material.get()));
		UINT materialNum = 16;
		ctx1->PSSetConstantBuffers1(1, 1, &bucketCB, &windowFirst, &materialNum);
	}
}

void GrassOptimizations::DrawBucket(GrassBucket& b)
{
	auto* ctx = globals::d3d::context;
	const uint64_t vertexDesc = b.drawResources.key.vertexDesc;
	auto* meshVB = b.drawResources.vertexBuffer.get();
	auto* indexB = b.drawResources.indexBuffer.get();
	const UINT vertexStride = VertexStrideFromDesc(vertexDesc);
	if (!meshVB || !indexB || !vertexStride)
		return;

	// Profile the technique as one pass, closed by RestoreTechnique.
	if (!drawPassOpen && globals::profiler->IsActive()) {
		globals::profiler->BeginPass("GrassOptimizations::IndirectDraw");
		drawPassOpen = true;
	}

	auto& shadowState = globals::game::shadowState->GetRuntimeData();
	static REL::Relocation<void (*)(uint32_t)> SetDirtyStates{ REL::RelocationID(75580, 77386) };
	if (shadowState.topology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
		shadowState.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		shadowState.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_PRIMITIVE_TOPO);
	}

	if (shadowState.vertexDesc != vertexDesc) {
		shadowState.vertexDesc = vertexDesc;
		shadowState.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_VERTEX_DESC);
	}

	// Only depth-writing draws contribute to the bucket's minimum alpha-test reference.
	if (shadowState.depthStencilDepthMode != RE::BSGraphics::DepthStencilDepthMode::kTestEqual) {
		const float alphaTestRef = shadowState.alphaTestEnabled ? std::max(0.0f, shadowState.alphaTestRef) : 0.0f;
		b.alphaTestRef = b.alphaTestRefKnown ? std::min(b.alphaTestRef, alphaTestRef) : alphaTestRef;
		b.alphaTestRefKnown = true;
	}

	// Bind material textures together instead of committing each slot separately.
	constexpr uint32_t kMaterialTextureBits = 0x7F;
	const uint32_t materialTextures = shadowState.PSResourceModifiedBits & kMaterialTextureBits;
	shadowState.PSResourceModifiedBits &= ~kMaterialTextureBits;

	if (leanCommitReady && shadowState.alphaTestRef == committedAlphaTestRef)
		shadowState.stateUpdateFlags.reset(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_TEST_REF);

	const bool leanCommit = leanCommitReady && shadowState.stateUpdateFlags.underlying() == 0 && !shadowState.PSResourceModifiedBits &&
	                        !shadowState.PSSamplerModifiedBits && globals::state->permutationData == globals::state->permutationDataPrevious;
	if (!leanCommit) {
		SetDirtyStates(0);
		committedAlphaTestRef = shadowState.alphaTestRef;
		leanCommitReady = true;
	}
	if (materialTextures) {
		const uint32_t firstTexture = std::countr_zero(materialTextures);
		const uint32_t textureCount = std::bit_width(materialTextures) - firstTexture;
		ctx->PSSetShaderResources(firstTexture, textureCount, reinterpret_cast<ID3D11ShaderResourceView* const*>(&shadowState.PSTexture[firstTexture]));
	}

	// Share complexity and canopy constants across LOD tiers.
	ID3D11Buffer* bucketCB = cullBucketCB->CB();
	UINT bucketFirst = b.cullSlot * (kSlotBytes / 16);
	UINT bucketNum = 16;
	ctx1->VSSetConstantBuffers1(13, 1, &bucketCB, &bucketFirst, &bucketNum);

	bool bindSharedViews = true;
	for (uint32_t tier = 0; tier < kTierCount; ++tier) {
		ID3D11Buffer* vertexBuffer = meshVB;
		ID3D11Buffer* indexBuffer = indexB;
		UINT stride = vertexStride;

		if (tier) {
			const auto* lod = b.lodMeshes[tier - 1];
			if (!lod)
				continue;

			vertexBuffer = lod->vertexBuffer;
			indexBuffer = lod->indexBuffer;
			stride = lod->vertexStride;

			if (shadowState.vertexDesc != lod->vertexDesc) {
				shadowState.vertexDesc = lod->vertexDesc;
				shadowState.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_VERTEX_DESC);
				SetDirtyStates(0);
			}
		}

		// A zero-stride instance stream satisfies the input layout while the shader reads cull records.
		ID3D11Buffer* vbs[2] = { vertexBuffer, vertexBuffer };
		const UINT strides[2] = { stride, 0 };
		const UINT offsets[2] = { 0, 0 };
		ctx->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R16_UINT, 0);
		ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);

		// The engine does not track VS resources, so only the record SRV changes between tiers.
		ID3D11ShaderResourceView* instanceViews[4] = { bucketStore.cullOutput.compactedSRVs[tier].get(), bucketStore.InstancePoolSRV(), bucketStore.GroupPoolSRV(), b.complexSRV };
		ctx->VSSetShaderResources(2, std::exchange(bindSharedViews, false) ? 4 : 1, instanceViews);

		ctx->DrawIndexedInstancedIndirect(bucketStore.arguments->resource.get(), (b.cullSlot * kTierCount + tier) * kArgsStride);
	}
}

void GrassOptimizations::EndDrawPass()
{
	leanCommitReady = false;
	if (std::exchange(drawPassOpen, false))
		globals::profiler->EndPass();
}

void GrassOptimizations::Hooks::BSGrassShader_RestoreTechnique::thunk(RE::BSShader* This, std::uint32_t technique)
{
	globals::features::grassOptimizations.EndDrawPass();
	func(This, technique);
}

void GrassOptimizations::Hooks::BSMultiStreamInstanceTriShape_dtor::thunk(RE::BSMultiStreamInstanceTriShape* shape)
{
	globals::features::grassOptimizations.bucketStore.StageRemoval(shape);
	func(shape);
}

GrassOptimizations::Hooks::CPUBufferScope::CPUBufferScope() :
	previous(std::exchange(active, globals::features::grassOptimizations.OwnsGrass()))
{}

RE::BSGraphics::VertexBuffer* GrassOptimizations::Hooks::CreateVertexBuffer::thunk(void* manager, std::uint32_t byteWidth, void* data, std::uint64_t vertexDesc)
{
	if (!CPUBufferScope::active || !globals::features::grassOptimizations.OwnsGrass() || !data)
		return func(manager, byteWidth, data, vertexDesc);

	// The engine accepts a null GPU buffer and frees both CPU allocations.
	auto* vertexBuffer = static_cast<RE::BSGraphics::VertexBuffer*>(RE::malloc(sizeof(RE::BSGraphics::VertexBuffer)));
	void* copy = vertexBuffer ? RE::malloc(byteWidth) : nullptr;
	if (!copy) {
		RE::free(vertexBuffer);
		return func(manager, byteWidth, data, vertexDesc);
	}

	std::memcpy(copy, data, byteWidth);
	vertexBuffer->buffer = nullptr;
	vertexBuffer->m_data = copy;
	vertexBuffer->byteWidth = byteWidth;

	return vertexBuffer;
}

std::uint32_t GrassOptimizations::Hooks::AddGroup::thunk(RE::BSMultiStreamInstanceTriShape* shape, std::uint32_t count, std::uint16_t* instanceData, std::uint32_t instanceShorts, float boundPadding)
{
	const auto index = WithOwnedGroups([&] { return func(shape, count, instanceData, instanceShorts, boundPadding); });
	globals::features::grassOptimizations.bucketStore.CaptureEngineGroup(shape, index);
	return index;
}

void GrassOptimizations::Hooks::DoneAddingInstances::thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSTArray<std::uint32_t>& groupIndices)
{
	AttachQueuedGroups::pendingCapture.store(true, std::memory_order_release);
	WithOwnedGroups([&] { func(shape, groupIndices); });
	AttachQueuedGroups::pendingCapture.store(true, std::memory_order_release);
}

void GrassOptimizations::Hooks::RemoveGroup::thunk(RE::BSMultiStreamInstanceTriShape* shape, std::uint32_t index)
{
	globals::features::grassOptimizations.bucketStore.RemoveEngineGroup(shape, index, func.get());
}

std::uint32_t GrassOptimizations::Hooks::AddGroupGIDBuffer::thunk(RE::BSMultiStreamInstanceTriShape* a1, RE::BSMultiStreamInstanceTriShape::GroupHeader* a2, std::uint16_t* a3)
{
	const auto index = WithOwnedGroups([&] { return func(a1, a2, a3); });
	globals::features::grassOptimizations.bucketStore.CaptureEngineGroup(a1, index);
	return index;
}

void GrassOptimizations::Hooks::AddQueuedGroupGIDBuffer::thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSMultiStreamInstanceTriShape::GroupHeader* header, std::uint16_t* instanceData, RE::BSTArray<std::uint32_t>& groupIndices)
{
	AttachQueuedGroups::pendingCapture.store(true, std::memory_order_release);
	WithOwnedGroups([&] { func(shape, header, instanceData, groupIndices); });
	AttachQueuedGroups::pendingCapture.store(true, std::memory_order_release);
}

void GrassOptimizations::Hooks::AddQueuedGroupGIDFile::thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSStream* stream, RE::BSTArray<std::uint32_t>& groupIndices)
{
	AttachQueuedGroups::pendingCapture.store(true, std::memory_order_release);
	WithOwnedGroups([&] { func(shape, stream, groupIndices); });
	AttachQueuedGroups::pendingCapture.store(true, std::memory_order_release);
}

void GrassOptimizations::Hooks::AttachQueuedGroups::thunk()
{
	const bool pending = pendingCapture.exchange(false, std::memory_order_acq_rel);
	func();

	// Preserve captures queued while attachment was running.
	if (pending || pendingCapture.load(std::memory_order_acquire))
		globals::features::grassOptimizations.bucketStore.CaptureQueuedGroups();
}

void GrassOptimizations::Hooks::AddGroupGIDFile::thunk(RE::BSMultiStreamInstanceTriShape* a1, RE::BSStream* a2)
{
	// The file loader does not return a group index, so identify the first changed slot.
	static thread_local std::vector<void*> groupsBefore;
	const auto& groups = a1->GetMultiStreamTrishapeRuntimeData().instanceGroups;
	groupsBefore.assign(groups.begin(), groups.end());

	WithOwnedGroups([&] { func(a1, a2); });

	uint32_t index = 0;
	while (index < groups.size() && (!groups[index] || (index < groupsBefore.size() && groups[index] == groupsBefore[index])))
		++index;

	if (index < groups.size())
		globals::features::grassOptimizations.bucketStore.CaptureEngineGroup(a1, index);
}

RE::BSMultiStreamInstanceTriShape* GrassOptimizations::Hooks::LoadGrassType::thunk(RE::BGSGrassManager* grassManager, RE::GrassParam* a_param, uint32_t CellXDivided, uint32_t CellYDivided, uint64_t* typeKey, RE::BSFixedString* modelPath)
{
	auto* shape = func(grassManager, a_param, CellXDivided, CellYDivided, typeKey, modelPath);

	if (auto& self = globals::features::grassOptimizations; self.OwnsGrass() && shape && modelPath)
		self.bucketStore.RegisterModel(shape, modelPath->c_str());

	return shape;
}

void GrassOptimizations::Hooks::NiNode_OnVisible::thunk(RE::NiNode* This, RE::NiCullingProcess* process, std::int32_t alphaGroupIndex)
{
	auto& self = globals::features::grassOptimizations;
	if (This != grassRoot.load(std::memory_order_relaxed) || !self.OwnsGrass())
		return func(This, process, alphaGroupIndex);

	// Preserve the engine's zero-radius visibility gate.
	if (std::bit_cast<std::uint32_t>(This->worldBound.radius) == 0 && !This->GetFlags().any(RE::NiAVObject::Flag::kAlwaysDraw))
		return;

	// Prefer the main view, with a fallback when it stops visiting the grass root.
	const auto frame = globals::game::graphicsState->frameCount;
	const bool mainView = process->camera && process->camera == RE::Main::WorldRootCamera();
	if (mainView)
		mainViewVisitFrame.store(frame, std::memory_order_relaxed);

	const bool queueOptimized = mainView || frame - mainViewVisitFrame.load(std::memory_order_relaxed) > 2;

	// Draw bucket proxies instead of traversing native grass shapes.
	const auto* snapshot = self.bucketStore.GetCullSnapshot();
	if (queueOptimized && snapshot) {
		for (const auto& group : snapshot->proxyGroups)
			process->AppendVirtual(*group.proxy, alphaGroupIndex);
	}
}

void GrassOptimizations::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* a2, std::uint32_t flags)
{
	auto& self = globals::features::grassOptimizations;

	// Cull with main-camera PerFrame data so shadows cannot change occlusion or fades.
	const auto frame = globals::game::graphicsState->frameCount;
	if (self.lastFrame != frame) {
		const auto* cam = RE::Main::WorldRootCamera();
		if (!cam || IsMainCameraPerFrame(cam->world.translate)) {
			self.UpdateGrass(cam);
			self.lastFrame = frame;
		}
	}

	func(This, a2, flags);

	auto* shadowState = globals::game::shadowState;
	shadowState->SetPSTextureAddressMode(0, RE::BSGraphics::TextureAddressMode::kClampSClampT);
	shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);
}

void GrassOptimizations::Hooks::BSGrassShader_SetupMaterial::thunk(RE::BSShader* This, const RE::BSLightingShaderMaterialBase* material)
{
	if (!globals::features::grassOptimizations.loaded)
		func(This, material);
}

void GrassOptimizations::Hooks::DrawVanilla(RE::BSMultiStreamInstanceTriShape* geometry)
{
	auto& self = globals::features::grassOptimizations;
	// Native draws invalidate the cached bucket state.
	self.leanCommitReady = false;

	auto* ctx = globals::d3d::context;
	auto& groups = geometry->GetMultiStreamTrishapeRuntimeData().instanceGroups;

	if (auto& cb = self.vanillaDrawCB) {
		ID3D11Buffer* marker = cb->CB();
		ctx->VSSetConstantBuffers(13, 1, &marker);
	}

	for (auto* curInstanceGroup : groups) {
		if (!curInstanceGroup || !curInstanceGroup->isVisible || !curInstanceGroup->vertexBuffer)
			continue;

		auto* vertexBuffer = curInstanceGroup->vertexBuffer;
		if (!vertexBuffer->buffer) {
			if (!vertexBuffer->m_data)
				continue;

			// CPU-only captures need a native buffer after optimized drawing is disabled.
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>(vertexBuffer->byteWidth);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			const D3D11_SUBRESOURCE_DATA data{ vertexBuffer->m_data, 0, 0 };
			winrt::com_ptr<ID3D11Buffer> buffer;
			if (FAILED(globals::d3d::device->CreateBuffer(&desc, &data, buffer.put())))
				continue;

			Util::SetResourceName(buffer.get(), "GrassOptimizations::NativeInstances");
			vertexBuffer->buffer = reinterpret_cast<REX::W32::ID3D11Buffer*>(buffer.detach());
		}

		static REL::Relocation<void (*)(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t, uint32_t, RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*)> DrawInstancedTriShape{ REL::RelocationID(75479, 77265) };
		DrawInstancedTriShape(globals::game::renderer, geometry->GetGeometryRuntimeData().rendererData, 0, geometry->GetTrishapeRuntimeData().triangleCount, curInstanceGroup->instanceCount, geometry->GetGeometryRuntimeData().vertexDesc, curInstanceGroup->vertexBuffer);
	}
}

void GrassOptimizations::Hooks::DrawInstanceTriShape::thunk(RE::BSRenderPass*, RE::BSMultiStreamInstanceTriShape* geometry)
{
	auto& self = globals::features::grassOptimizations;
	const auto* snapshot = self.ActiveSnapshot();

	if (const auto* group = snapshot ? snapshot->FindGroup(geometry) : nullptr) {
		bool first = true;

		for (auto* bucket : group->buckets) {
			if (!self.IsBucketDrawable(*bucket))
				continue;

			if (!std::exchange(first, false))
				self.SetupOptimizedPass(*bucket);

			self.DrawBucket(*bucket);
		}

	} else {
		DrawVanilla(geometry);
	}
}

void GrassOptimizations::Hooks::Install()
{
	auto& trampoline = SKSE::GetTrampoline();

	stl::write_vfunc<0x0, BSMultiStreamInstanceTriShape_dtor>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);
	stl::write_vfunc<0x34, NiNode_OnVisible>(RE::VTABLE_NiNode[0]);
	stl::write_vfunc<0x3A, DoneAddingInstances>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);
	stl::write_vfunc<0x3C, AddGroup>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);
	stl::write_vfunc<0x3D, RemoveGroup>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);

	stl::write_vfunc<0x1E, CreateVertexBuffer>(RE::VTABLE_BSShaderResourceManager[0]);

	stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);
	stl::write_vfunc<0x4, BSGrassShader_SetupMaterial>(RE::VTABLE_BSGrassShader[0]);
	stl::write_vfunc<0x3, BSGrassShader_RestoreTechnique>(RE::VTABLE_BSGrassShader[0]);

	// Queued cache groups receive their indices during attachment.
	stl::write_thunk_call<AddQueuedGroupGIDBuffer>(REL::RelocationID(15205, 15373).address() + Util::VersionedRelocation::Select(0x7FF, 0x756, 0x768));
	stl::write_thunk_call<AddQueuedGroupGIDFile>(REL::RelocationID(15206, 15374).address() + REL::Relocate(0x394, 0x384));
	stl::detour_thunk<AttachQueuedGroups>(REL::RelocationID(74603, 76335));

	stl::write_thunk_call<AddGroupGIDBuffer>(REL::RelocationID(15205, 15373).address() + Util::VersionedRelocation::Select(0x806, 0x75D, 0x76F));
	stl::write_thunk_call<AddGroupGIDFile>(REL::RelocationID(15206, 15374).address() + REL::Relocate(0x39B, 0x38B));

	// NGIO can call the loader outside the engine's grass tasks.
	stl::detour_thunk<LoadGrassType>(REL::RelocationID(15214, 15383));

	std::uint8_t patch[] = { 0x4C, 0x89, 0xF2 };  // mov rdx, r14
	REL::safe_write(REL::RelocationID(100847, 107637).address() + REL::Relocate(0x660, 0x648), patch, sizeof(patch));
	stl::write_thunk_call<DrawInstanceTriShape>(REL::RelocationID(100847, 107637).address() + REL::Relocate(0x663, 0x64B));
	trampoline.write_branch<5>(REL::RelocationID(100847, 107637).address() + REL::Relocate(0x668, 0x650), REL::RelocationID(100847, 107637).address() + REL::Relocate(0x759, 0x73A));

	// Skip mapping the vanilla dynamic fade buffer.
	if (REL::Module::IsAE()) {
		trampoline.write_branch<5>(
			REL::RelocationID(99996, 106685).address() + Util::VersionedRelocation::Select(0x595, 0x595, 0x6A2),
			REL::RelocationID(99996, 106685).address() + Util::VersionedRelocation::Select(0x6C6, 0x6C6, 0x7D6));
	} else {
		REL::safe_write(REL::RelocationID(99996, 106685).address() + 0x54D, REL::NOP5);
	}

	logger::info("[GRASS OPTIMIZATIONS] Installed hooks");
}
