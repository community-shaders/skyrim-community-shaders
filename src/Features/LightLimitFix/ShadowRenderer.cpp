// Shadow rendering operations for LightLimitFix.
// Contains: resource setup, per-frame data copy, and shadow-specific UI.

#include "../LightLimitFix.h"
#include "Deferred.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"

// Fills a ShadowData entry from a light's shadowmap descriptor transform.
// Mirrors the former Deferred::SetShadowParameters private template.
template <typename T>
static void SetShadowParameters(T& lightData, Deferred::ShadowData& sd)
{
	if (lightData.shadowmapDescriptors.empty())
		return;
	auto& desc = lightData.shadowmapDescriptors[0];
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
	DirectX::XMStoreFloat4x4(&sd.ShadowProj, proj);

	DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
	DirectX::XMStoreFloat4x4(&sd.InvShadowProj, invProj);

	sd.ShadowParam.z = (lightData.shadowBiasScale * 0.00025f) / 3.0f;
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

	uint32_t plCount = 0;
	uint32_t unshadowedLights = 0;
	ShadowCasterManager::ForEachShadowLight(shadowSceneNode->GetRuntimeData().shadowLightsAccum,
		[&](RE::BSShadowLight* light) {
			// Use the stable container-slot index from s_lights rather than reading
			// shadowmapDescriptors[0].shadowmapIndex, which may have been corrupted by
			// ReturnShadowmaps() (called via Hook_DisableColorMask) after ScheduleShadowCasters
			// fixed it but before this function runs.
			int32_t stableSlot = ShadowCasterManager::GetShadowSlot(light);
			if (stableSlot < 0 || static_cast<uint32_t>(stableSlot) >= slots) {
				unshadowedLights++;
				plCount++;
				return;
			}
			uint32_t depthSlot = static_cast<uint32_t>(stableSlot);

			{
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
			}

			plCount++;
		});

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
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
	if (menuOpen) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints(ImVec2(240, 150), ImVec2(800, 1200));
		ImGui::Begin("LLF Shadow Slots", nullptr, ImGuiWindowFlags_NoSavedSettings);
	} else {
		ImGui::SetNextWindowPos(ImVec2(pos, pos), ImGuiCond_Always);
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
		} else
			ShadowCasterManager::DrawOverlayShadowModeInfo(mode, shadowUnshadowedLightCount, lightCount);
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
