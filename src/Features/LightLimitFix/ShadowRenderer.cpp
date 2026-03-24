// Shadow rendering operations for LightLimitFix.
// Contains: resource setup, per-frame data copy, and shadow-specific UI.

#include "../LightLimitFix.h"
#include "Deferred.h"
#include "State.h"

// Fills a ShadowData entry from a light's shadowmap descriptor transform.
// Mirrors the former Deferred::SetShadowParameters private template.
template <typename T>
static void SetShadowParameters(T& lightData, Deferred::ShadowData& sd)
{
	auto& desc = lightData.shadowmapDescriptors[0];
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
	DirectX::XMStoreFloat4x4(&sd.ShadowProj, proj);

	DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
	DirectX::XMStoreFloat4x4(&sd.InvShadowProj, invProj);
}

// ─── Resource setup ───────────────────────────────────────────────────────────

void LightLimitFix::SetupShadowResources()
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

// ─── Per-frame shadow data copy ───────────────────────────────────────────────

void LightLimitFix::EarlyPrepass()
{
	auto state = globals::state;
	state->BeginPerfEvent("LLF CopyPointShadowData");
	CopyPointShadowData();
	state->EndPerfEvent();
}

void LightLimitFix::CopyPointShadowData()
{
	ZoneScoped;
#ifdef TRACY_ENABLE
	TracyD3D11Zone(globals::state->tracyCtx, "LLF CopyPointShadowData");
#endif

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
	uint32_t prevSlotUsage = ShadowCasterManager::GetSlotUsage();
	ShadowCasterManager::BeginSlotFrame(slots);
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* shadowMapsSRV =
		globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS].depthSRV;

	auto& shadowAccum = shadowSceneNode->GetRuntimeData().shadowLightsAccum;
	uint32_t plCount = 0;
	uint32_t unshadowedLights = 0;
	int mapIndex = 0;
	while (true) {
		auto light = shadowAccum[mapIndex];
		if (!light)
			break;

		mapIndex += light->shadowMapCount;

		if (plCount < slots) {
			uint32_t depthSlot = globals::game::isVR ?
			                         light->GetVRRuntimeData().shadowmapDescriptors[0].shadowmapIndex :
			                         light->GetRuntimeData().shadowmapDescriptors[0].shadowmapIndex;

			float shadowTypeF = light->GetIsParabolicLight() ? float(light->shadowMapCount == 2 ? 2 : 1) : 0.f;
			sd[depthSlot].ShadowParam.x = shadowTypeF;

			if (globals::game::isVR)
				SetShadowParameters(light->GetVRRuntimeData(), sd[depthSlot]);
			else
				SetShadowParameters(light->GetRuntimeData(), sd[depthSlot]);

			float range = light->light->GetLightRuntimeData().radius.x;
			// -1.0 sentinel: shader returns 0.0 (fully dark) → light invisible.
			// 0.0 means unwritten slot → shader returns 1.0 (fully lit, no shadow).
			uintptr_t lightKey = reinterpret_cast<uintptr_t>(light);
			sd[depthSlot].ShadowParam.y = ShadowCasterManager::IsSuppressed(lightKey) ? -1.0f : range;
			ShadowCasterManager::RecordSlot(depthSlot,
				{ static_cast<uint32_t>(shadowTypeF), range, true, lightKey });
		} else {
			unshadowedLights++;
		}

		plCount++;
	}

	if (plCount != shadowLightCount || ShadowCasterManager::GetSlotUsage() != prevSlotUsage || unshadowedLights != shadowUnshadowedLightCount) {
		shadowLightCount = plCount;
		shadowUnshadowedLightCount = unshadowedLights;
		if (unshadowedLights > 0)
			logger::debug("[LLF] {} shadow lights, {} / {} slots used; {} lights dropped (no shadow)",
				plCount, ShadowCasterManager::GetSlotUsage(), slots, unshadowedLights);
		else
			logger::debug("[LLF] {} shadow lights, {} / {} slots used", plCount, ShadowCasterManager::GetSlotUsage(), slots);
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

// ─── Debug helpers ────────────────────────────────────────────────────────────

std::string LightLimitFix::BuildShadowSlotColorLegend() const
{
	const auto& shadowSlotInfos = ShadowCasterManager::GetSlotInfos();
	if (shadowSlotInfos.empty())
		return {};

	std::string out = "Shadow Slot Color Map (Mode 8):\n";
	for (uint32_t i = 0; i < static_cast<uint32_t>(shadowSlotInfos.size()); ++i) {
		const auto& info = shadowSlotInfos[i];
		if (!info.valid)
			continue;

		float hue = fmodf(float(i) * 0.618033988f, 1.0f);
		ImVec4 c = ShadowCasterManager::ShadowSlotHueColor(i);
		auto ri = static_cast<uint8_t>(c.x * 255.0f);
		auto gi = static_cast<uint8_t>(c.y * 255.0f);
		auto bi = static_cast<uint8_t>(c.z * 255.0f);

		out += std::format("  Slot {:2d} | hue {:5.3f} | #{:02X}{:02X}{:02X} | {:11s} | r={:.0f}\n",
			i, hue, ri, gi, bi, ShadowCasterManager::GetShadowTypeName(info.type), info.range);
	}
	return out;
}

// ─── Shadow-specific settings UI ─────────────────────────────────────────────

void LightLimitFix::DrawShadowSamplingSettings()
{
	ImGui::SeparatorText("Shadow Sampling");

	{
		static constexpr const char* filterModeNames[] = { "Gather (Single-tap)", "PCF (Poisson disc)", "PCSS (Contact Hardened)" };
		int mode = magic_enum::enum_integer(settings.FilterMode);
		if (ImGui::Combo("Shadow Filter", &mode, filterModeNames, IM_ARRAYSIZE(filterModeNames)))
			settings.FilterMode = magic_enum::enum_cast<ShadowFilterMode>(mode).value_or(ShadowFilterMode::PCF);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Shadow filtering quality for shadow-casting point/spot lights.\n\n"
				"Fast (Single-tap): Single gather tap with adaptive slope bias.\n"
				"  Smooth 2x2 comparison; fastest option. Good default.\n\n"
				"Soft (PCF): 8-tap rotated Poisson disc. Softer, more uniform penumbra.\n"
				"  Use Kernel Scale to adjust softness.\n\n"
				"Contact Hardened (PCSS): Penumbra width scales with distance from\n"
				"  the occluder. Shadows are sharp at contact, soft in open air.");

		if (magic_enum::enum_integer(settings.FilterMode) >= magic_enum::enum_integer(ShadowFilterMode::PCF)) {
			ImGui::SliderFloat("Kernel Scale", &settings.KernelScale, 0.1f, 8.0f, "%.2f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Multiplier on the base PCF filter radius. Higher values give softer shadows.");
		}

		if (settings.FilterMode == ShadowFilterMode::PCSS) {
			ImGui::SliderFloat("Light Size", &settings.LightSize, 1.0f, 50.0f, "%.1f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Virtual light source size in shadow map pixels for PCSS penumbra estimation. Larger values give wider soft shadows further from the caster.");
		}
	}

	ShadowCasterManager::DrawSettings(settings.ShadowSettings);
}

// ─── Overlay ─────────────────────────────────────────────────────────────────

void LightLimitFix::DrawOverlay()
{
	// Overlay shows when visualization is active OR when slots are suppressed
	// (so the suppression list stays visible without the debug visualizer running).
	bool vizOn = settings.EnableLightsVisualisation;
	bool hasSuppressed = ShadowCasterManager::HasSuppressedLights();
	if (!vizOn && !hasSuppressed)
		return;

	// When the CS menu is open, show a draggable/resizable window so the user can
	// move it out of the way and expand the table.  When the menu is closed, keep
	// it as a compact pinned overlay (no title bar, no chrome).
	bool menuOpen = globals::menu->IsEnabled;
	if (menuOpen) {
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints(ImVec2(240, 150), ImVec2(800, 1200));
		ImGui::Begin("LLF Shadow Slots", nullptr, ImGuiWindowFlags_NoSavedSettings);
	} else {
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
		ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(540, 640));
		ImGui::Begin("##LLFDebug", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	}

	if (vizOn) {
		static const char* kVizNames[] = {
			"Light Limit", "Strict Lights Count", "Clustered Lights Count",
			"Shadow Mask", "Shadow Light Count", "Point Light Shadow Factor",
			"Unshadowed Point Lights", "Shadow Caster Density",
			"Shadow Slot Index Color", "Light Type Visualization"
		};
		uint32_t m = settings.LightsVisualisationMode;
		const char* vizName = (m < IM_ARRAYSIZE(kVizNames)) ? kVizNames[m] : "Unknown";
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "LLF DEBUG - %s", vizName);
	} else
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "LLF - Shadow Suppression");
	ImGui::Separator();

	uint32_t mode = vizOn ? settings.LightsVisualisationMode : UINT32_MAX;
	const uint32_t slotUsage = ShadowCasterManager::GetSlotUsage();

	// ── Per-mode informational panels (visualization only) ──────────────────
	if (vizOn) {
		if (mode <= 1) {
			ImGui::Text("Clustered lights : %u / %u", lightCount, MAX_LIGHTS);
			ImGui::Text("Shadow lights    : %u / %u slots", slotUsage, globals::deferred->shadowMapSlots);
			if (shadowUnshadowedLightCount > 0)
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Dropped (overflow): %u", shadowUnshadowedLightCount);
		} else if (mode == 2) {
			if (lightCount >= MAX_LIGHTS)
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Clustered lights : %u / %u  OVERFLOW", lightCount, MAX_LIGHTS);
			else
				ImGui::Text("Clustered lights : %u / %u  (%.0f%%)", lightCount, MAX_LIGHTS, 100.f * lightCount / MAX_LIGHTS);
			uint32_t cx = clusterSize[0], cy = clusterSize[1], cz = clusterSize[2];
			ImGui::Text("Cluster grid     : %ux%ux%u  (%u total)", cx, cy, cz, cx * cy * cz);
			ImGui::Text("Max lights/cluster: %u", CLUSTER_MAX_LIGHTS);
		} else if (mode == 3) {
			ImGui::Text("R channel  = directional soft shadow");
			ImGui::Text("G channel  = directional detailed shadow");
			ImGui::TextDisabled("(B = unused)");
			ImGui::Spacing();
			ImGui::Text("Shadow slots     : %u / %u", slotUsage, globals::deferred->shadowMapSlots);
		} else if (mode >= 4 && mode <= 6) {
			ImGui::Text("Shadow lights    : %u valid,  %u dropped", slotUsage, shadowUnshadowedLightCount);
			ImGui::Text("Total clustered  : %u", lightCount);
			if (mode == 4)
				ImGui::TextDisabled("Pixel heatmap: 0=blue  8+=red");
			else if (mode == 5)
				ImGui::TextDisabled("White = fully lit,  black = fully in shadow");
			else
				ImGui::TextDisabled("Pixel heatmap: 0=blue  8+=red (lights without shadow maps)");
		} else if (mode == 7) {
			uint32_t slots = globals::deferred->shadowMapSlots;
			ImGui::Text("Slots used / total : %u / %u", slotUsage, slots);
			if (shadowUnshadowedLightCount > 0)
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Overflow (red)     : %u lights", shadowUnshadowedLightCount);
			ImGui::TextDisabled("Cool  Turbo[0.0-0.3] = 1-4 shadows");
			ImGui::TextDisabled("Warm  Turbo[0.3-0.8] = 5-%u shadows", slots);
			ImGui::TextDisabled("Red                  = overflow");
		} else if (mode == 9) {
			uint32_t spotC = 0, hemiC = 0, omniC = 0;
			for (const auto& info : ShadowCasterManager::GetSlotInfos()) {
				if (!info.valid)
					continue;
				if (info.type == 0)
					spotC++;
				else if (info.type == 1)
					hemiC++;
				else
					omniC++;
			}
			ImGui::Text("R  Spot (frustum)   : %u", spotC);
			ImGui::Text("G  Hemisphere       : %u", hemiC);
			ImGui::Text("B  Omni (paraboloid): %u", omniC);
			ImGui::Text("   Unshadowed        : %u", shadowUnshadowedLightCount);
			if (shadowUnshadowedLightCount > 0)
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "   Overflow (red)    : %u", shadowUnshadowedLightCount);
		}
	}

	// ── Shadow slot toggle table ─────────────────────────────────────
	// Show when in a shadow-related viz mode, or when lights are suppressed.
	bool shadowRelatedMode = !vizOn || (mode >= 4);
	if (hasSuppressed || shadowRelatedMode) {
		if (vizOn)
			ImGui::Separator();
		ShadowCasterManager::DrawShadowLightTable(!menuOpen, vizOn && (mode == 8), true);
	}

	ImGui::End();
}
