// Shadow rendering operations for LightLimitFix.
// Contains: resource setup, per-frame data copy, and shadow-specific UI.

#include "../LightLimitFix.h"
#include "Deferred.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"

// Fills a ShadowLightData entry from a light's shadowmap descriptor transform.
// Returns true on success, false when the light has no usable descriptors --
// the caller must treat false as "do not advertise a valid shadow for this
// slot", because ShadowProj remains at its default zero matrix and the
// shader's depth-comparison sampling against that matrix collapses to
// "fully shadowed" (the worst possible visual outcome -- e.g. grass goes
// pitch black under any shadow-flagged point light). Pair this with a
// ShadowParam.y = 0 fallback in the caller so the shader's safe sentinel
// (`if (ShadowLightParam.y == 0) return 1.0;`) keeps the slot fully lit
// instead of fully dark.
template <typename T>
static bool SetShadowParameters(T& lightData, Deferred::ShadowLightData& sd)
{
	if (lightData.shadowmapDescriptors.empty())
		return false;

	auto& desc = lightData.shadowmapDescriptors[0];
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
	DirectX::XMStoreFloat4x4(&sd.ShadowProj, proj);

	DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
	DirectX::XMStoreFloat4x4(&sd.InvShadowProj, invProj);

	sd.ShadowParam.z = lightData.shadowBiasScale * 0.00025f;
	return true;
}

// ─── Per-frame shadow data copy ───────────────────────────────────────────────

void LightLimitFix::EarlyPrepass()
{
	auto state = globals::state;
	state->BeginPerfEvent("LLF CopyShadowLightData");
	CopyShadowLightData();
	state->EndPerfEvent();
}

void LightLimitFix::CopyShadowLightData()
{
	ZoneScoped;
#ifdef TRACY_ENABLE
	TracyD3D11Zone(globals::state->tracyCtx, "LLF CopyShadowLightData");
#endif

	uint32_t slots = ShadowCasterManager::GetInstalledSlotCount();
	if (slots == 0)
		return;

	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	// Lazy (re)allocation when slot count changes (e.g. on resolution change).
	if (!shadowLights || shadowLightsCapacity != slots) {
		delete shadowLights;
		shadowLights = nullptr;

		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(Deferred::ShadowLightData);
		sbDesc.ByteWidth = slots * sizeof(Deferred::ShadowLightData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = slots;

		shadowLights = new Buffer(sbDesc);
		shadowLights->CreateSRV(srvDesc);
		shadowLightsCapacity = slots;
	}

	std::vector<Deferred::ShadowLightData> sd(slots);
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
			if (stableSlot < 0) {
				// Sun (BSShadowDirectionalLight) — no kSHADOWMAPS slice. Its
				// shadow lives in kSHADOWMAPS_ESRAM and is sampled through a
				// separate path (DirectionalShadowCascades at t99). Skip
				// silently so we don't count it as an "unshadowed point
				// light" or scribble garbage into sd[0].
				return;
			}
			if (static_cast<uint32_t>(stableSlot) >= slots) {
				unshadowedLights++;
				plCount++;
				return;
			}
			uint32_t depthSlot = static_cast<uint32_t>(stableSlot);

			{
				float shadowTypeF = light->GetIsParabolicLight() ? float(light->shadowMapCount == 2 ? 2 : 1) : 0.f;
				sd[depthSlot].ShadowParam.x = shadowTypeF;

				const bool projValid = globals::game::isVR ?
			                               SetShadowParameters(light->GetVRRuntimeData(), sd[depthSlot]) :
			                               SetShadowParameters(light->GetRuntimeData(), sd[depthSlot]);

				float range = light->light->GetLightRuntimeData().radius.x;
				// ShadowParam.y semantics in the shader:
				//   > 0  → valid radius; sample kSHADOWMAPS via ShadowProj at the slot.
				//   == 0 → safe sentinel; shader returns 1.0 (fully lit, no shadow).
				//   < 0  → suppression sentinel; shader returns 0.0 (fully dark).
				// If SetShadowParameters skipped (empty descriptors -> ShadowProj
				// stays default zero matrix), we MUST leave ShadowParam.y at 0 so
				// the safe sentinel fires. Otherwise the shader samples a zero
				// projection -> depth comparison says fully shadowed -> any
				// shadow-flagged light with stale descriptors makes grass go
				// pitch black under that light.
				uintptr_t lightKey = reinterpret_cast<uintptr_t>(light);
				const bool suppressed = ShadowCasterManager::IsSuppressed(lightKey);
				sd[depthSlot].ShadowParam.y = suppressed ? -1.0f : (projValid ? range : 0.0f);
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
		DX::ThrowIfFailed(context->Map(shadowLights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		memcpy(mapped.pData, sd.data(), slots * sizeof(Deferred::ShadowLightData));
		context->Unmap(shadowLights->resource.get(), 0);
		ID3D11ShaderResourceView* srv = shadowLights->srv.get();
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
	// Overlay shows when:
	//   - visualisation modes are active (debug heatmaps), OR
	//   - any light is suppressed (so the suppression list stays accessible), OR
	//   - any debug override is in effect (pin shadow / pin convert / solo) so
	//     users can find what they pinned without remembering to toggle anything, OR
	//   - the user explicitly opted in via Show Shadow Overlay (lets the table's
	//     debug controls — cycle button, solo, hover-pulse — be reachable in
	//     the default state without first triggering a side-effect).
	bool vizOn = settings.EnableLightsVisualisation;
	bool hasSuppressed = ShadowCasterManager::HasSuppressedLights();
	bool hasOverrides = ShadowCasterManager::HasAnyOverrides();
	bool showOverlay = settings.ShowShadowOverlay;
	if (!vizOn && !hasSuppressed && !hasOverrides && !showOverlay)
		return;

	// When the CS menu is open, show a draggable/resizable window so the user can
	// move it out of the way and expand the table.  When the menu is closed, keep
	// it as a compact pinned overlay (no title bar, no chrome).
	bool menuOpen = globals::menu->IsEnabled;
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();

	// Single unified window: same ImGui ID across menu open/closed so the
	// user's resize persists. Title bar and Move are toggled via flags --
	// hidden when the menu is closed (pinned debug overlay) and shown when
	// the menu is open (so the user can drag/resize via the title bar).
	// We deliberately don't pass NoSavedSettings so ImGui retains the size
	// the user picked across sessions.
	ImGuiWindowFlags flags = ImGuiWindowFlags_None;
	if (!menuOpen)
		flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove;

	ImGui::SetNextWindowPos(ImVec2(pos, pos), menuOpen ? ImGuiCond_Appearing : ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(280, 200), ImVec2(800, 1200));
	ImGui::Begin("LLF Shadow Slots", nullptr, flags);

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

	// ── All stats grouped above the table (same order as menu) ─────────
	// Summary always visible. Scheduler stats only when not in a viz mode
	// that has its own legend competing for the same space.
	ShadowCasterManager::DrawShadowSummary(lightCount, MAX_LIGHTS, shadowUnshadowedLightCount);
	if (!vizOn)
		ShadowCasterManager::DrawShadowSchedulerStats();

	// ── Per-mode informational panels (visualization-mode-specific only) ──
	if (vizOn) {
		if (mode == 2) {
			uint32_t cx = clusterSize[0], cy = clusterSize[1], cz = clusterSize[2];
			ImGui::Text("Cluster grid       : %ux%ux%u  (%u total)", cx, cy, cz, cx * cy * cz);
			ImGui::Text("Max lights/cluster : %u", CLUSTER_MAX_LIGHTS);
		} else if (mode >= 3) {
			ShadowCasterManager::DrawOverlayShadowModeInfo(mode, shadowUnshadowedLightCount, lightCount);
		}
	}

	// ── Shadow slot toggle table ─────────────────────────────────────
	// Show when in a shadow-related viz mode, or when lights are suppressed.
	// readOnly=true when the menu is closed -- the overlay isn't interactive
	// then, so the per-row Mode/Solo buttons would be dead pixels. readOnly
	// also bounds the table height so the stats above stay visible even
	// when many lights are present (the table scrolls internally instead
	// of pushing the window past its max-height constraint).
	bool shadowRelatedMode = !vizOn || (mode >= 4);
	if (hasSuppressed || shadowRelatedMode) {
		ImGui::Separator();
		// compact=false in the overlay: the table fills the remaining
		// content region of the user-sized window and scrolls internally
		// (ScrollY in Util::ShowSortedStringTableCustom). Stats above stay
		// visible regardless of how many lights exist or how the user has
		// resized the window. readOnly is still true when the menu is
		// closed -- buttons would be dead pixels.
		ShadowCasterManager::DrawShadowLightTable(false, vizOn && (mode == 8), true, !menuOpen);
	}

	ImGui::End();
}
