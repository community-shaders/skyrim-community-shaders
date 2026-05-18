#include "LightLimitFix.h"
#include "InverseSquareLighting.h"
#include "LinearLighting.h"
#include "Utils/UI.h"

#include "Deferred.h"
#include "Menu/ThemeManager.h"
#include "Shadercache.h"
#include "State.h"
#include "Utils/ExternalEmittance.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightLimitFix::Settings,
	ShowShadowOverlay,
	ShadowSettings)

void LightLimitFix::DrawSettings()
{
	auto shaderCache = globals::shaderCache;

	ShadowCasterManager::DrawSettings(settings.ShadowSettings);

	// ---- Active Shadow Casters --------------------------------------
	// One cohesive section: overlay toggle, then ALL the stats grouped
	// together (summary + scheduler stats + budget verdict), then the
	// table below. Same layout as the overlay so testers see the same
	// thing in both views with the stats above the (potentially long)
	// table -- no scrolling required to find the headline numbers.
	ImGui::SeparatorText("Shadow Limit Fix -- Active Casters");

	ImGui::Checkbox("Show Shadow Overlay", &settings.ShowShadowOverlay);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Pop out an always-visible overlay window with the shadow caster table.\n"
			"Without this, the overlay only appears when a light is suppressed\n"
			"or a visualisation mode is active. Enable to access the table's\n"
			"debug controls (cycle button, solo, Shift+hover pulse) any time.");
	}

	ShadowCasterManager::DrawShadowSummary(lightCount, MAX_LIGHTS, shadowUnshadowedLightCount);
	ShadowCasterManager::DrawShadowSchedulerStats();
	ImGui::Separator();
	ShadowCasterManager::DrawShadowLightTable(true, false);

	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Light Limit Visualization")) {
		ImGui::Checkbox("Enable Lights Visualisation", &settings.EnableLightsVisualisation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables visualization of the light limit\n");
		}

		{
			static const char* comboOptions[] = {
				"Light Limit",
				"Strict Lights Count",
				"Clustered Lights Count",
				"Shadow Mask",
				"Shadow Light Count",
				"Point Light Shadow Factor",
				"Unshadowed Point Lights",
				"Shadow Caster Density",
				"Shadow Slot Index Color",
				"Light Type Visualization",
			};
			ImGui::Combo("Lights Visualisation Mode", (int*)&settings.LightsVisualisationMode, comboOptions, IM_ARRAYSIZE(comboOptions));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Light Limit: Red when the strict light limit is reached (>=7 portal-strict lights).\n"
					"\n"
					"Strict Lights Count: Heatmap of portal-strict lights per pixel (blue=0, red=15).\n"
					"\n"
					"Clustered Lights Count: Heatmap of dynamic lights in each screen tile (blue=0, red=128).");
				ShadowCasterManager::DrawVisualisationTooltipShadowModes();
			}
		}

		currentEnableLightsVisualisation = settings.EnableLightsVisualisation;
		if (previousEnableLightsVisualisation != currentEnableLightsVisualisation) {
			globals::state->SetDefines(settings.EnableLightsVisualisation ? "LLFDEBUG" : "");
			shaderCache->Clear(RE::BSShader::Type::Lighting);
			previousEnableLightsVisualisation = currentEnableLightsVisualisation;
		}

		ImGui::TreePop();
	}
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.ShadowMapSlots = ShadowCasterManager::GetInstalledSlotCount();
	std::copy(clusterSize, clusterSize + 3, perFrame.ClusterSize);
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	return perFrame;
}

void LightLimitFix::SetupResources()
{
	auto screenSize = globals::state->screenSize;
	if (REL::Module::IsVR())
		screenSize.x *= .5;
	clusterSize[0] = ((uint)screenSize.x + 63) / 64;
	clusterSize[1] = ((uint)screenSize.y + 63) / 64;
	clusterSize[2] = 32;
	uint clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

	{
		std::vector<std::pair<const char*, const char*>> clusterDefines;
		if (REL::Module::IsVR())
			clusterDefines = { { "VR", "" } };
		clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", clusterDefines, "cs_5_0");
		clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", clusterDefines, "cs_5_0");

		lightBuildingCB = new ConstantBuffer(ConstantBufferDesc<LightBuildingCB>());
		lightCullingCB = new ConstantBuffer(ConstantBufferDesc<LightCullingCB>());
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = clusterCount;

		sbDesc.StructureByteStride = sizeof(ClusterAABB);
		sbDesc.ByteWidth = sizeof(ClusterAABB) * numElements;
		clusters = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Clusters");
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexCounter = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexCounter");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexList = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexList");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightGrid");
		srvDesc.Buffer.NumElements = numElements;
		lightGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightGrid->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(LightData);
		sbDesc.ByteWidth = sizeof(LightData) * MAX_LIGHTS;
		lights = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Lights");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_LIGHTS;
		lights->CreateSRV(srvDesc);
	}

	{
		strictLightDataCB = new ConstantBuffer(ConstantBufferDesc<StrictLightDataCB>());
	}
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

void LightLimitFix::LoadSettings(json& o_json)
{
	settings = o_json;
	// iShadowMapResolution:Display is owned by Skyrim's INI, not our JSON.
	ShadowCasterManager::LoadINISettings();

	// Raise saved values below the current floor so older configs migrate.
	if (settings.ShadowSettings.MaxRedrawPerFrame < ShadowCasterManager::Settings::kMinMaxRedrawPerFrame)
		settings.ShadowSettings.MaxRedrawPerFrame = ShadowCasterManager::Settings::kMinMaxRedrawPerFrame;
}

void LightLimitFix::SaveSettings(json& o_json)
{
	o_json = settings;
	ShadowCasterManager::SaveINISettings();
}

RE::NiNode* GetParentRoomNode(RE::NiAVObject* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	static const auto* roomRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSMultiBoundRoom }.get();
	static const auto* portalRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSPortalSharedNode }.get();

	const auto* rtti = object->GetRTTI();
	if (rtti == roomRtti || rtti == portalRtti) {
		return static_cast<RE::NiNode*>(object);
	}

	return GetParentRoomNode(object->parent);
}

void LightLimitFix::BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	strictLightDataTemp.NumStrictLights = 0;
	strictLightDataTemp.ShadowBitMask = 0;

	strictLightDataTemp.RoomIndex = -1;
	if (!roomNodes.empty()) {
		if (RE::NiNode* roomNode = GetParentRoomNode(a_pass->geometry)) {
			if (auto it = roomNodes.find(roomNode); it != roomNodes.cend()) {
				strictLightDataTemp.RoomIndex = it->second;
			}
		}
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass)
{
	auto& isl = globals::features::inverseSquareLighting;

	auto accumulator = *globals::game::currentAccumulator.get();
	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == globals::game::smState->shadowSceneNode[0];

	strictLightDataTemp.NumStrictLights = inWorld ? 0 : (a_pass->numLights - 1);

	uint32_t writeIdx = 0;
	for (uint32_t i = 0; i < strictLightDataTemp.NumStrictLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		if (!bsLight)
			continue;
		auto niLight = bsLight->light.get();
		if (!niLight)
			continue;

		auto& runtimeData = niLight->GetLightRuntimeData();

		LightData light{};
		light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
		light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

		if (isl.loaded) {
			isl.ProcessLight(light, bsLight, niLight);
		} else {
			light.radius = runtimeData.radius.x;
			// light.color *= runtimeData.fade;
			light.fade = runtimeData.fade;
		}

		light.fade *= bsLight->lodDimmer;

		SetLightPosition(light, niLight->world.translate, inWorld);

		if (i < a_pass->numShadowLights) {
			auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
			auto checkDescs = [&](auto& runtimeData) {
				if (!runtimeData.shadowmapDescriptors.empty()) {
					light.shadowMapIndex = runtimeData.shadowmapDescriptors[0].shadowmapIndex;
					light.lightFlags.set(LightFlags::Shadow);
				}
			};
			if (globals::game::isVR)
				checkDescs(shadowLight->GetVRRuntimeData());
			else
				checkDescs(shadowLight->GetRuntimeData());
		}

		strictLightDataTemp.StrictLights[writeIdx++] = light;
	}
	strictLightDataTemp.NumStrictLights = writeIdx;

	for (uint32_t i = 0; i < a_pass->numShadowLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		if (!bsLight)
			continue;
		auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
		GET_INSTANCE_MEMBER(maskIndex, shadowLight);
		if (maskIndex < 32)
			strictLightDataTemp.ShadowBitMask |= (1u << maskIndex);
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	auto shaderCache = globals::shaderCache;
	auto context = globals::d3d::context;
	auto smState = globals::game::smState;

	if (!shaderCache->IsEnabled())
		return;

	auto accumulator = *globals::game::currentAccumulator.get();

	auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto isEmpty = strictLightDataTemp.NumStrictLights == 0;
	const bool isWorld = accumulator->GetRuntimeData().activeShadowSceneNode == shadowSceneNode;
	const auto roomIndex = strictLightDataTemp.RoomIndex;
	const auto shadowBitMask = strictLightDataTemp.ShadowBitMask;

	if (!isEmpty || (isEmpty && !wasEmpty) || isWorld != wasWorld || previousRoomIndex != roomIndex || shadowBitMask != previousShadowBitMask) {
		strictLightDataCB->Update(strictLightDataTemp);
		wasEmpty = isEmpty;
		wasWorld = isWorld;
		previousRoomIndex = roomIndex;
		previousShadowBitMask = shadowBitMask;
	}

	if (frameChecker.IsNewFrame()) {
		ID3D11Buffer* buffer = { strictLightDataCB->CB() };
		context->PSSetConstantBuffers(3, 1, &buffer);
	}
}

void LightLimitFix::SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached)
{
	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		RE::NiPoint3 eyePosition;

		if (a_cached) {
			eyePosition = eyePositionCached[eyeIndex];
		} else {
			eyePosition = Util::GetEyePosition(eyeIndex);
		}

		auto worldPos = a_initialPosition - eyePosition;
		a_light.positionWS[eyeIndex].data.x = worldPos.x;
		a_light.positionWS[eyeIndex].data.y = worldPos.y;
		a_light.positionWS[eyeIndex].data.z = worldPos.z;
	}
}

void LightLimitFix::Prepass()
{
	auto context = globals::d3d::context;

	auto state = globals::state;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "LightLimitFix Prepass");
	state->BeginPerfEvent("LightLimitFix Prepass");
	ShadowCasterManager::Update(settings.ShadowSettings, globals::game::smState->shadowSceneNode[0], nullptr);
	UpdateLights();

	ID3D11ShaderResourceView* views[3]{};
	views[0] = lights->srv.get();
	views[1] = lightIndexList->srv.get();
	views[2] = lightGrid->srv.get();
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);

	state->EndPerfEvent();
}

bool LightLimitFix::IsValidLight(RE::BSLight* a_light)
{
	return a_light && a_light->light && a_light->light.get() && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool LightLimitFix::IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

void LightLimitFix::PostPostLoad()
{
	Hooks::Install();
	ShadowCasterManager::Init(settings.ShadowSettings);
	ShadowCasterManager::Install(settings.ShadowSettings);
}

void LightLimitFix::DataLoaded()
{
	auto iMagicLightMaxCount = globals::game::gameSettingCollection->GetSetting("iMagicLightMaxCount");
	iMagicLightMaxCount->data.i = MAXINT32;
	logger::info("[LLF] Unlocked magic light limit");
}

void LightLimitFix::ClearShaderCache()
{
	if (clusterBuildingCS) {
		clusterBuildingCS->Release();
		clusterBuildingCS = nullptr;
	}
	if (clusterCullingCS) {
		clusterCullingCS->Release();
		clusterCullingCS = nullptr;
	}
	std::vector<std::pair<const char*, const char*>> clusterDefines;
	if (REL::Module::IsVR())
		clusterDefines = { { "VR", "" } };
	clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", clusterDefines, "cs_5_0");
	clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", clusterDefines, "cs_5_0");
}

void LightLimitFix::UpdateLights()
{
	ZoneScopedN("LLF::UpdateLights");
	auto smState = globals::game::smState;
	auto& isl = globals::features::inverseSquareLighting;

	auto shadowSceneNode = smState->shadowSceneNode[0];

	// Cache data since cameraData can become invalid in first-person

	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		auto eyePosition = globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
		eyePositionCached[eyeIndex] = { eyePosition.x, eyePosition.y, eyePosition.z };
	}

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);

	// Process point lights

	roomNodes.clear();

	auto addRoom = [&](RE::NiNode* node, LightData& light) {
		uint8_t roomIndex = 0;
		if (auto it = roomNodes.find(node); it == roomNodes.cend()) {
			roomIndex = static_cast<uint8_t>(roomNodes.size());
			roomNodes.insert_or_assign(node, roomIndex);
		} else {
			roomIndex = it->second;
		}
		light.roomFlags.SetBit(roomIndex, 1);
	};

	// Hover-pulse helper: if the table has a hovered row matching this light's
	// pointer, replace the cluster colour with a magenta pulse so the user can
	// see which light a row corresponds to in 3D. Pulse cycles ~once per second
	// using ImGui::GetTime() for a stable visual signal.
	auto applyDebugOverrides = [](LightData& light, const void* lightPtr) {
		auto hoverKey = ShadowCasterManager::GetHoveredLight();
		if (hoverKey != 0 && reinterpret_cast<uintptr_t>(lightPtr) == hoverKey) {
			float t = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.2831853f);
			light.color = { 1.0f, 0.0f, 1.0f };  // magenta
			light.fade = 4.0f + t * 4.0f;        // pulsed intensity
		}
	};

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				// IsSuppressed includes solo (every key except the soloed one is
				// implicitly suppressed). This filters every non-shadow cluster
				// light through the user's debug overrides.
				if (ShadowCasterManager::IsSuppressed(reinterpret_cast<uintptr_t>(bsLight)))
					return;
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						// light.color *= runtimeData.fade;
						light.fade = runtimeData.fade;
					}

					light.fade *= bsLight->lodDimmer;

					if (!IsGlobalLight(bsLight)) {
						// List of BSMultiBoundRooms affected by a light
						for (const auto& roomPtr : bsLight->rooms) {
							addRoom(roomPtr, light);
						}
						// List of BSPortals affected by a light
						for (const auto& portalPtr : bsLight->portals) {
							addRoom(portalPtr->portalSharedNode.get(), light);
						}
						light.lightFlags.set(LightFlags::PortalStrict);
					}

					SetLightPosition(light, niLight->world.translate);

					applyDebugOverrides(light, bsLight);

					if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
						lightsData.push_back(light);
					}
				}
			}
		}
	};

	auto addShadowLight = [&](RE::BSShadowLight* shadowLight, bool castsShadow, uint32_t shadowSlot = 0) {
		if (IsValidLight(shadowLight)) {
			if (auto niLight = shadowLight->light.get()) {
				auto& runtimeData = niLight->GetLightRuntimeData();

				LightData light{};
				light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
				light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

				if (isl.loaded) {
					isl.ProcessLight(light, shadowLight, niLight);
				} else {
					light.radius = runtimeData.radius.x;
					// light.color *= runtimeData.fade;
					light.fade = runtimeData.fade;
				}

				light.fade *= shadowLight->lodDimmer;

				if (!IsGlobalLight(shadowLight)) {
					// List of BSMultiBoundRooms affected by a light
					for (const auto& roomPtr : shadowLight->rooms) {
						addRoom(roomPtr, light);
					}
					// List of BSPortals affected by a light
					for (const auto& portalPtr : shadowLight->portals) {
						addRoom(portalPtr->portalSharedNode.get(), light);
					}
					light.lightFlags.set(LightFlags::PortalStrict);
				}

				if (castsShadow) {
					// Use the caller-provided stable slot index from s_lights rather than
					// shadowmapDescriptors[0].shadowmapIndex, which may be corrupted by
					// ReturnShadowmaps() called via Hook_DisableColorMask after
					// ScheduleShadowCasters was run this frame.
					light.shadowMapIndex = shadowSlot;
					light.lightFlags.set(LightFlags::Shadow);
				}

				SetLightPosition(light, niLight->world.translate);

				applyDebugOverrides(light, shadowLight);

				if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
					lightsData.push_back(light);
				}
			}
		}
	};

	// Single pass over shadowLightsAccum:
	//   - Builds shadowLightPtrs so activeLights below skips lights already added here.
	//   - Calls addShadowLight for each logical light.
	// EnableLight calls both GameEnableLight (→ activeLights) and
	// GameSetShadowCasterSlot (→ shadowLightsAccum) for redrawn lights, so without
	// the skip below each redrawn shadow light would be added twice.
	std::unordered_set<RE::BSLight*> shadowLightPtrs;
	ShadowCasterManager::ForEachShadowLight(shadowSceneNode->GetRuntimeData().shadowLightsAccum,
		[&](RE::BSShadowLight* light) {
			shadowLightPtrs.insert(light);
			// GetShadowSlot returns the kSHADOWMAPS texture slot:
			//   -1 : sun (no kSHADOWMAPS slice — sun shadows live in kSHADOWMAPS_ESRAM
			//        and are sampled via the directional cascade path, not the cluster
			//        loop). Skip cluster injection entirely. The sun stays in
			//        shadowLightPtrs so the activeLights loop below doesn't re-add it.
			//   >=0: kSHADOWMAPS slice index (0..ShadowMapSlots-1) post-reclaim.
			int32_t stableSlot = ShadowCasterManager::GetShadowSlot(light);
			if (stableSlot < 0)
				return;
			bool castsShadow = static_cast<uint32_t>(stableSlot) < ShadowCasterManager::GetInstalledSlotCount();
			addShadowLight(light, castsShadow, castsShadow ? static_cast<uint32_t>(stableSlot) : 0u);
		});

	for (auto& e : shadowSceneNode->GetRuntimeData().activeLights) {
		if (auto bsLight = e.get(); bsLight && shadowLightPtrs.count(bsLight))
			continue;  // shadow light: already added above with correct Shadow flag
		addLight(e);
	}

	// Converted shadow lights (shadow lights demoted to normal-light overflow handling
	// via SCM's ConvertExcessToNormal) live in the engine's activeShadowLights list
	// (offset 0x148) — verified via Ghidra against ShadowSceneNode AE 1.6.1170. They
	// are NOT migrated to activeLights (0x130) when our Hook_IsShadowLight reports
	// false, because the engine's AddLight just searches the existing wrappers and
	// activates the matching one in-place rather than moving entries between lists.
	//
	// Iterate SCM's s_normalConvert directly rather than scanning activeShadowLights:
	// only lights actually in s_normalConvert are intended to render as non-shadow.
	// activeShadowLights also contains BSShadowLights that are merely active shadow
	// casters this frame (already handled via shadowLightsAccum above), and could in
	// principle contain disabled-but-not-yet-removed entries. Iterating the convert
	// list is both tighter (no false positives) and cheaper.
	//
	// Without this, ConvertExcessToNormal lights have no entry in the cluster
	// lightsData[] and never render — the user-visible "converted lights are
	// invisible" symptom of issue #2121 #3.
	ShadowCasterManager::ForEachConvertedLight([&](RE::BSShadowLight* light) {
		auto* asBs = static_cast<RE::BSLight*>(light);
		if (shadowLightPtrs.count(asBs))
			return;  // simultaneously a shadow caster this frame; already added
		// Honour the user's suppression toggle in the shadow caster table:
		// converted lights share the same lightKey suppression set as shadow
		// lights, so suppressing one in the table hides it whether it's
		// rendering as a shadow caster or demoted to non-shadow.
		if (ShadowCasterManager::IsSuppressed(reinterpret_cast<uintptr_t>(light)))
			return;
		// Engine zeroes lodDimmer when its shadow-distance LOD cull fires
		// (BSShadowParabolicLight_UpdateCamera test 2, gated on the lodFade
		// flag -- not a visibility test, see ShadowCasterManager.cpp's
		// Ghidra-verified comment). Without restoration, addLight()'s
		// `light.fade *= lodDimmer` would zero the contribution and the
		// (color*fade > 1e-4) filter would drop the light entirely.
		//
		// Restore only when fully zeroed. Any smooth fade value the engine
		// set (between 0 and 1) is preserved -- those represent the engine's
		// own gradual distance attenuation, which is correct to honour for
		// cluster lighting. Overriding unconditionally was producing
		// distant always-full-bright converted lights that ignored the
		// engine's intended fade-with-distance.
		if (light->lodDimmer == 0.0f)
			light->lodDimmer = 1.0f;
		addLight(RE::NiPointer<RE::BSLight>(asBs));
	});

	auto context = globals::d3d::context;

	lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	size_t bytes = sizeof(LightData) * lightCount;
	memcpy_s(mapped.pData, bytes, lightsData.data(), bytes);
	context->Unmap(lights->resource.get(), 0);

	UpdateStructure();

	// Single-shot consumption: clear the hover key after the cluster has read it.
	// The table re-sets it every frame the cursor is hovering a row with Shift
	// held, so the pulse continues smoothly while hovering. As soon as the menu
	// closes (or the cursor leaves the table, or Shift is released), the table
	// stops re-setting the key and the pulse vanishes on the next frame.
	ShadowCasterManager::SetHoveredLight(0);
}

void LightLimitFix::UpdateStructure()
{
	auto context = globals::d3d::context;

	lightsNear = *globals::game::cameraNear;
	lightsFar = *globals::game::cameraFar;

	auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
	if (REL::Module::IsVR())
		renderSize.x *= .5;
	clusterSize[0] = ((uint)renderSize.x + 63) / 64;
	clusterSize[1] = ((uint)renderSize.y + 63) / 64;
	clusterSize[2] = 32;

	{
		LightBuildingCB updateData{};
		updateData.LightsNear = lightsNear;
		updateData.LightsFar = lightsFar;
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightBuildingCB->Update(updateData);

		ID3D11Buffer* buffer = lightBuildingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11UnorderedAccessView* clusters_uav = clusters->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &clusters_uav, nullptr);

		context->CSSetShader(clusterBuildingCS, nullptr, 0);
		context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);

		ID3D11UnorderedAccessView* null_uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
	}

	{
		LightCullingCB updateData{};
		updateData.LightCount = lightCount;
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightCullingCB->Update(updateData);

		UINT counterReset[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightIndexCounter->uav.get(), counterReset);

		ID3D11Buffer* buffer = lightCullingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11ShaderResourceView* srvs[] = { clusters->srv.get(), lights->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { lightIndexCounter->uav.get(), lightIndexList->uav.get(), lightGrid->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(clusterCullingCS, nullptr, 0);
		context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr };
	context->CSSetShaderResources(0, 2, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[3] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 3, null_uavs, nullptr);
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	func(This, Pass, RenderFlags);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	// Defensive pre-call guard: BSEffectShader::SetupGeometry iterates
	// Pass->sceneLights[i] and dereferences bsLight->light->fade
	// (BSLight+0x48 -> NiLight+0x134) with NO null check. Stale entries are
	// possible because Pass->sceneLights[] is a raw BSLight** (not
	// NiPointer<>): the engine's pass cache can outlive individual lights
	// or capture them after their NiLight has been cleared. Crashes seen in
	// the wild include garbage data (BSLight memory recycled as a string
	// buffer) and outright NULL NiLight (engine half-destroyed the BSLight
	// but it's still ref-counted alive in some list).
	//
	// Walk the array and clamp numLights to the count of entries that the
	// engine can safely dereference. Validation:
	//   - BSLight* is canonical, 8-byte aligned, non-null
	//   - bsLight->light pointer is canonical, 8-byte aligned, non-null
	// Entries failing either check stop the loop; the engine's own loop
	// bails on the first bad entry too, so clamping matches its contract.
	if (Pass && Pass->sceneLights && Pass->numLights > 0) {
		const auto isPlausible = [](const void* p) {
			const auto v = reinterpret_cast<std::uintptr_t>(p);
			return v >= 0x10000 && v < 0x800000000000ull && (v & 0x7) == 0;
		};
		std::uint8_t validCount = 0;
		for (std::uint8_t i = 0; i < Pass->numLights; ++i) {
			RE::BSLight* bsLight = Pass->sceneLights[i];
			if (!isPlausible(bsLight)) {
				static int loggedBsLight = 0;
				if (loggedBsLight++ < 10) {
					logger::warn(
						"[LLF] BSEffectShader_SetupGeometry: bad BSLight* at "
						"sceneLights[{}]=0x{:x} numLights={}; clamping to {}",
						i, reinterpret_cast<std::uintptr_t>(bsLight), Pass->numLights, validCount);
				}
				break;
			}
			RE::NiLight* niLight = bsLight->light.get();
			if (!isPlausible(niLight)) {
				// Catches both NULL (engine cleared the NiPointer) and
				// garbage (BSLight memory recycled). NULL is the more common
				// observed failure -- the engine's loop has no null check
				// before reading [+0x134].
				static int loggedNiLight = 0;
				if (loggedNiLight++ < 10) {
					logger::warn(
						"[LLF] BSEffectShader_SetupGeometry: bad NiLight at "
						"sceneLights[{}] (BSLight=0x{:x} NiLight=0x{:x}); clamping to {}",
						i,
						reinterpret_cast<std::uintptr_t>(bsLight),
						reinterpret_cast<std::uintptr_t>(niLight),
						validCount);
				}
				break;
			}
			++validCount;
		}
		if (validCount < Pass->numLights)
			Pass->numLights = validCount;
	}

	func(This, Pass, RenderFlags);
	ExternalEmittance::UpdatePermutation(Pass);
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

void LightLimitFix::Hooks::BSWaterShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};
