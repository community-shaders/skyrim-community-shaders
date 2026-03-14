#include "LightLimitFix.h"
#include "InverseSquareLighting.h"
#include "LinearLighting.h"

#include "Deferred.h"
#include "Menu/ThemeManager.h"
#include "Shadercache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightLimitFix::Settings,
	FilterMode,
	KernelScale,
	LightSize)

static constexpr uint CLUSTER_MAX_LIGHTS = 128;
static constexpr uint MAX_LIGHTS = 1024;

void LightLimitFix::DrawSettings()
{
	auto shaderCache = globals::shaderCache;
	auto deferred = globals::deferred;

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (lightCount >= MAX_LIGHTS)
			ImGui::TextColored({ 1, 0.3f, 0.3f, 1 }, "Clustered Lights : %u / %u (overflow!)", lightCount, MAX_LIGHTS);
		else
			ImGui::Text("Clustered Lights : %u / %u", lightCount, MAX_LIGHTS);

		uint32_t shadowSlots = deferred->shadowMapSlots;
		if (shadowUnshadowedLightCount > 0)
			ImGui::TextColored({ 1, 0.3f, 0.3f, 1 }, "Shadow Lights    : %u lights, %u / %u slots (%u dropped)", shadowLightCount, shadowSlotUsage, shadowSlots, shadowUnshadowedLightCount);
		else
			ImGui::Text("Shadow Lights    : %u lights, %u / %u slots", shadowLightCount, shadowSlotUsage, shadowSlots);

		ImGui::TreePop();
	}

	///////////////////////////////
	ImGui::SeparatorText("Shadow Sampling");

	{
		static constexpr const char* filterModeNames[] = { "Fast", "Soft (PCF)", "Contact Hardened (PCSS)" };
		int mode = static_cast<int>(settings.FilterMode);
		if (ImGui::Combo("Shadow Filter", &mode, filterModeNames, IM_ARRAYSIZE(filterModeNames)))
			settings.FilterMode = static_cast<uint32_t>(mode);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Quality of shadow filtering for shadow-casting point lights.\nPCF softens shadow edges; PCSS additionally scales softness by distance from the caster.");

		if (settings.FilterMode >= 1) {
			ImGui::SliderFloat("Kernel Scale", &settings.KernelScale, 0.1f, 8.0f, "%.2f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Multiplier on the base PCF filter radius. Higher values give softer shadows.");
		}

		if (settings.FilterMode == 2) {
			ImGui::SliderFloat("Light Size", &settings.LightSize, 1.0f, 50.0f, "%.1f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Virtual light source size in shadow map pixels for PCSS penumbra estimation. Larger values give wider soft shadows further from the caster.");
		}
	}

	///////////////////////////////
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
			};
			ImGui::Combo("Lights Visualisation Mode", (int*)&settings.LightsVisualisationMode, comboOptions, IM_ARRAYSIZE(comboOptions));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Light Limit: Red when the strict light limit is reached (>=7 portal-strict lights).\n"
					"\n"
					"Strict Lights Count: Heatmap of portal-strict lights per pixel (blue=0, red=15).\n"
					"\n"
					"Clustered Lights Count: Heatmap of dynamic lights in each screen tile (blue=0, red=128).\n"
					"\n"
					"Shadow Mask: R=directional soft shadow, G=directional detailed shadow.\n"
					"\n"
					"Shadow Light Count: Heatmap of shadow-casting point/spot lights per pixel (blue=0, red=8+).\n"
					"Use to gauge shadow density; high counts indicate expensive shadow sampling.\n"
					"\n"
					"Point Light Shadow Factor: Brightness shows the darkest shadow value from any point/spot\n"
					"light. White=fully lit, black=fully shadowed. Shows where PCF/PCSS filtering is active.\n"
					"\n"
					"Unshadowed Point Lights: Heatmap of point/spot lights without shadow maps (blue=0, red=8+).\n"
					"High values where lights are bright indicate where the shadow slot limit is costing quality.");
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

void LightLimitFix::DrawOverlay()
{
	if (!settings.EnableLightsVisualisation)
		return;
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
	ImGui::SetNextWindowPos(ImVec2(pos, pos), ImGuiCond_Always);
	ImGui::Begin("##LLFDebug", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "DEBUG FEATURE - LIGHT LIMIT VISUALISATION ENABLED");
	ImGui::End();
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.FilterMode = settings.FilterMode;
	perFrame.KernelScale = settings.KernelScale;
	perFrame.LightSize = settings.LightSize;
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
		clusters = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexCounter = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexList = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc);
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
		lights = eastl::make_unique<Buffer>(sbDesc);

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

	{
		// PCF comparison sampler bound to s14 for point/spot shadow sampling.
		D3D11_SAMPLER_DESC cmpDesc = {};
		cmpDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		cmpDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		cmpDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		cmpDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		cmpDesc.MaxAnisotropy = 1;
		cmpDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		cmpDesc.MinLOD = 0;
		cmpDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&cmpDesc, &shadowCmpSampler));
	}
}

// Fills a ShadowData entry from a light's shadowmap descriptor transform.
// Mirrors the former Deferred::SetShadowParameters private template.
template <typename T>
static void SetShadowParameters(T& lightData, Deferred::ShadowData& sd)
{
	auto& desc = lightData.shadowmapDescriptors[0];
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
	DirectX::XMStoreFloat4x4(&sd.ShadowProj, proj);

	DirectX::XMMATRIX invProj = DirectX::XMMatrixTranspose(proj);
	DirectX::XMStoreFloat4x4(&sd.InvShadowProj, invProj);
}

void LightLimitFix::EarlyPrepass()
{
	CopyPointShadowData();
}

void LightLimitFix::CopyPointShadowData()
{
	ZoneScoped;

	auto deferred = globals::deferred;
	uint32_t slots = deferred->shadowMapSlots;
	if (slots == 0)
		return;

	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	// Lazy (re)allocation when slot count changes (e.g. on resolution change).
	if (!perShadows || perShadowsCapacity != slots) {
		delete perShadows;
		perShadows = nullptr;

		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(Deferred::ShadowData);
		sbDesc.ByteWidth = slots * sizeof(Deferred::ShadowData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = slots;

		perShadows = new Buffer(sbDesc);
		perShadows->CreateSRV(srvDesc);
		perShadowsCapacity = slots;
	}

	std::vector<Deferred::ShadowData> sd(slots);
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* shadowMapsSRV =
		globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS].depthSRV;

	// shadowLightsAccum is the CPU-side slot-indexed mirror of kSHADOWMAPS:
	// shadowLightsAccum[i] holds the light whose shadow was rendered into kSHADOWMAPS[i]
	// during Main_RenderShadowMaps.
	uint32_t plCount = 0;
	uint32_t unshadowedLights = 0;
	uint32_t slotUsage = 0;
	int mapIndex = 0;
	while (true) {
		RE::BSShadowLight* light = shadowSceneNode->GetRuntimeData().shadowLightsAccum[mapIndex];
		if (!light)
			break;

		uint32_t depthSlot = globals::game::isVR ?
		                         light->GetVRRuntimeData().shadowmapDescriptors[0].shadowmapIndex :
		                         light->GetRuntimeData().shadowmapDescriptors[0].shadowmapIndex;

		if (plCount < slots) {
			sd[depthSlot].ShadowParam.x = light->GetIsParabolicLight() ? float(light->shadowMapCount == 2 ? 2 : 1) : 0.f;

			if (globals::game::isVR)
				SetShadowParameters(light->GetVRRuntimeData(), sd[depthSlot]);
			else
				SetShadowParameters(light->GetRuntimeData(), sd[depthSlot]);

			sd[depthSlot].ShadowParam.y = light->light->GetLightRuntimeData().radius.x;
			slotUsage += 1;  // each light occupies exactly 1 texture-array slot (DPB packs both hemispheres)
		} else {
			unshadowedLights++;
		}

		mapIndex += light->shadowMapCount;
		plCount++;
	}

	if (plCount != shadowLightCount || slotUsage != shadowSlotUsage || unshadowedLights != shadowUnshadowedLightCount) {
		shadowLightCount = plCount;
		shadowSlotUsage = slotUsage;
		shadowUnshadowedLightCount = unshadowedLights;
		if (unshadowedLights > 0)
			logger::debug("[LLF] {} shadow lights, {} / {} slots used; {} lights dropped (no shadow)",
				plCount, slotUsage, slots, unshadowedLights);
		else
			logger::debug("[LLF] {} shadow lights, {} / {} slots used", plCount, slotUsage, slots);
	}

	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(perShadows->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		memcpy(mapped.pData, sd.data(), slots * sizeof(Deferred::ShadowData));
		context->Unmap(perShadows->resource.get(), 0);
		ID3D11ShaderResourceView* srv = perShadows->srv.get();
		context->PSSetShaderResources(100, 1, &srv);
	}

	context->PSSetShaderResources(101, 1, &shadowMapsSRV);
	context->PSSetSamplers(14, 1, &shadowCmpSampler);
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

void LightLimitFix::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LightLimitFix::SaveSettings(json& o_json)
{
	o_json = settings;
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

	for (uint32_t i = 0; i < strictLightDataTemp.NumStrictLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		auto niLight = bsLight->light.get();

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
			if (globals::game::isVR)
				light.shadowMapIndex = shadowLight->GetVRRuntimeData().shadowmapDescriptors[0].shadowmapIndex;
			else
				light.shadowMapIndex = shadowLight->GetRuntimeData().shadowmapDescriptors[0].shadowmapIndex;
			light.lightFlags.set(LightFlags::Shadow);
		}

		strictLightDataTemp.StrictLights[i] = light;
	}

	for (uint32_t i = 0; i < a_pass->numShadowLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
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

	state->BeginPerfEvent("LightLimitFix Prepass");
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

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
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

					if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
						lightsData.push_back(light);
					}
				}
			}
		}
	};

	// Build a set of all shadow lights so we can skip them in activeLights and avoid
	// double-contribution if shadow lights appear in both activeLights and shadowLightsAccum.
	std::unordered_set<RE::BSLight*> shadowLightSet;
	{
		int mapIndex = 0;
		while (true) {
			RE::BSShadowLight* sl = shadowSceneNode->GetRuntimeData().shadowLightsAccum[mapIndex];
			if (!sl)
				break;
			shadowLightSet.insert(static_cast<RE::BSLight*>(sl));
			mapIndex += sl->shadowMapCount;
		}
	}

	for (auto& e : shadowSceneNode->GetRuntimeData().activeLights) {
		if (!shadowLightSet.count(e.get()))
			addLight(e);
	}

	auto addShadowLight = [&](RE::BSShadowLight* shadowLight, bool castsShadow) {
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
					if (globals::game::isVR)
						light.shadowMapIndex = shadowLight->GetVRRuntimeData().shadowmapDescriptors[0].shadowmapIndex;
					else
						light.shadowMapIndex = shadowLight->GetRuntimeData().shadowmapDescriptors[0].shadowmapIndex;
					light.lightFlags.set(LightFlags::Shadow);
				}

				SetLightPosition(light, niLight->world.translate);

				if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
					lightsData.push_back(light);
				}
			}
		}
	};

	{
		int bufferIndex = 0;
		int mapIndex = 0;
		while (true) {
			RE::BSShadowLight* light = shadowSceneNode->GetRuntimeData().shadowLightsAccum[mapIndex];
			if (!light)
				break;

			// Only set Shadow flag for lights with a valid written slot.
			// Overflow lights still use addShadowLight for correct color/radius setup,
			// but without the Shadow flag so the HLSL does not do a shadow map lookup
			// with a stale or out-of-range shadowMapIndex.
			addShadowLight(light, bufferIndex < (int)globals::deferred->shadowMapSlots);

			mapIndex += light->shadowMapCount;
			bufferIndex++;
		}
	}

	auto context = globals::d3d::context;

	lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	size_t bytes = sizeof(LightData) * lightCount;
	memcpy_s(mapped.pData, bytes, lightsData.data(), bytes);
	context->Unmap(lights->resource.get(), 0);

	UpdateStructure();
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
	func(This, Pass, RenderFlags);
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
