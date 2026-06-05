#include "TerrainBlending.h"

#include "Deferred.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "VR.h"

#define I18N_KEY_PREFIX "feature.terrain_blending."

#include <intrin.h>
#include <sstream>
#include <unordered_set>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainBlending::Settings,
	Enabled)

namespace
{
	std::atomic_uint32_t renderShadowmasksPhaseDepth{ 0 };

	bool IsInRenderShadowmasksPhase()
	{
		return renderShadowmasksPhaseDepth.load(std::memory_order_relaxed) != 0;
	}

	struct EngineHookTechniqueOverrideState
	{
		bool active = false;
		ID3D11ShaderResourceView* previousObbSrv = nullptr;
		ID3D11ShaderResourceView* previousShadowmaskSrv = nullptr;
		bool depthStateForced = false;
		ID3D11DepthStencilState* previousDepthStencilState = nullptr;
		ID3D11DepthStencilState* forcedDepthStencilState = nullptr;
		UINT previousStencilRef = 0;
	};

	EngineHookTechniqueOverrideState engineHookTechniqueState{};

	// Engine-hook override map for Utility shadowmask passes:
	// 1) PS slot 17 override: bind TB-selected depth SRV for OBB depth reads; prevents occlusion instability / mesh popping.
	// 2) PS slot 2 override: bind TB-selected depth SRV for shadowmask reads; prevents unstable/moving ground shadow imprint, and dark overlay style artifacts.
	// 3) OM depth override: force DepthFunc=ALWAYS only on descriptor 0x1062002; mitigate shadowmask ground artifacts caused by failed depth testing in 0x1062002.
	// All override paths below are gated by IsEngineHookFeatureGateSatisfied and all are VR-specific at runtime (isVR, gateSatisfied).
	// Developer Mode only: logs one hook snapshot per session ([TB Override]/[TB DepthOverride]) and explicit fallback activate/reset events.
	// Fallbacks: caller fallback is in ShouldAllowCallerWithFallback(...) (2 and 3 widen after 5 rejects and collapse on first allowlisted hit), SRV-source fallback is in Util::GetCurrentSceneDepthSRV(...).
	// Pixel descriptors:
	// - 0x262002 -> apply (1) + (2)
	// - 0x1062002 -> apply (1) + (2) + (3)
	constexpr uint32_t kShadowmaskDepthDescriptor0 = 0x262002u;
	constexpr uint32_t kShadowmaskDepthDescriptor1 = 0x1062002u;

	// Module RVAs from _ReturnAddress() at hooked engine callsites.
	// Ownership:
	// - Shared slot2 + depth-override callers: 0x1351AD4, 0xDBDD68
	// - Depth-override-only caller: 0x1349B7F
	const uint32_t kCallerRvaSlot2AndDepthOverrideA = static_cast<uint32_t>(REL::Relocate(0u, 0u, 0x1351AD4u));
	const uint32_t kCallerRvaSlot2AndDepthOverrideB = static_cast<uint32_t>(REL::Relocate(0u, 0u, 0xDBDD68u));
	const uint32_t kCallerRvaDepthOverrideOnly = static_cast<uint32_t>(REL::Relocate(0u, 0u, 0x1349B7Fu));

	// Slot2 rewrite allowlist (PS slot 2 = shadowmask depth SRV override path).
	// Includes only callsites validated for shadowmask slot2 rebinding.
	const std::array<uint32_t, 2> kSlot2CallerAllowlistRvas = {
		kCallerRvaSlot2AndDepthOverrideA,
		kCallerRvaSlot2AndDepthOverrideB
	};
	// Descriptor-scoped OM depth override allowlist (0x1062002 only).
	// Contains the two shared callers above plus one depth-override-only caller.
	const std::array<uint32_t, 3> kDepthOverrideCallerAllowlistRvas = {
		kCallerRvaSlot2AndDepthOverrideB,
		kCallerRvaSlot2AndDepthOverrideA,
		kCallerRvaDepthOverrideOnly
	};
	constexpr bool kEnableAutoBroadSlot2Fallback = true;
	constexpr uint64_t kSlot2AutoFallbackRejectThreshold = 5;
	constexpr bool kEnableAutoBroadDepthOverrideFallback = true;
	constexpr uint64_t kDepthOverrideAutoFallbackRejectThreshold = 5;

	struct CallerFallbackState
	{
		bool broadFallbackActive = false;
		bool sawAllowlistedHit = false;
		uint64_t rejectTotal = 0;
		std::unordered_set<uint32_t> blockedCallerRvas{};
		bool hookActiveLogged = false;
		bool fallbackActivatedLogged = false;
		uint32_t fallbackTriggerRva = 0;
		bool gateActivePrevious = false;
	};

	CallerFallbackState slot2FallbackState{};
	CallerFallbackState depthOverrideFallbackState{};

	void ResetCallerFallbackState(CallerFallbackState& a_state)
	{
		a_state.broadFallbackActive = false;
		a_state.sawAllowlistedHit = false;
		a_state.rejectTotal = 0;
		a_state.blockedCallerRvas.clear();
		a_state.fallbackActivatedLogged = false;
		a_state.fallbackTriggerRva = 0;
	}

	bool IsDiagnosticSlot2GuardMode()
	{
		return globals::state && globals::state->IsDeveloperMode();
	}

	// Caller identity must come from _ReturnAddress() at each hook callsite.
	// Normalize to module-relative RVA so values are stable across process ASLR.
	uint32_t ToModuleRva(const void* a_returnAddress)
	{
		return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(a_returnAddress) - REL::Module::get().base());
	}

	bool ShouldUseBlendedDepthSRV()
	{
		auto& vr = globals::features::vr;
		return !globals::game::isVR || !vr.gDepthBufferCulling || !*vr.gDepthBufferCulling;
	}

	bool IsShadowmaskDepthDescriptorWhitelisted(const uint32_t a_descriptor)
	{
		return a_descriptor == kShadowmaskDepthDescriptor0 || a_descriptor == kShadowmaskDepthDescriptor1;
	}

	template <size_t N>
	bool IsCallerAllowlisted(const std::array<uint32_t, N>& a_allowlist, const uint32_t a_callerRva)
	{
		for (const auto rva : a_allowlist) {
			if (rva == a_callerRva) {
				return true;
			}
		}
		return false;
	}

	template <size_t N>
	bool ShouldAllowCallerWithFallback(
		CallerFallbackState& a_state,
		const std::array<uint32_t, N>& a_allowlist,
		const bool a_enableAutoBroadFallback,
		const uint64_t a_rejectThreshold,
		const bool a_requireNoAllowlistedHitForFallback,
		const char* a_logPrefix,
		const char* a_fallbackLabel,
		const uint32_t a_callerRva)
	{
		if (IsCallerAllowlisted(a_allowlist, a_callerRva)) {
			a_state.sawAllowlistedHit = true;
			// For sensitive paths (depth override), collapse broad fallback as soon as
			// a known-good allowlisted caller is observed.
			if (a_requireNoAllowlistedHitForFallback && a_state.broadFallbackActive) {
				a_state.broadFallbackActive = false;
			}
			return true;
		}

		if (a_state.broadFallbackActive) {
			return true;
		}

		a_state.rejectTotal++;
		a_state.blockedCallerRvas.insert(a_callerRva);

		const bool fallbackEligible = !a_requireNoAllowlistedHitForFallback || !a_state.sawAllowlistedHit;
		if (a_enableAutoBroadFallback && fallbackEligible && a_state.rejectTotal >= a_rejectThreshold) {
			a_state.broadFallbackActive = true;
			a_state.fallbackTriggerRva = a_callerRva;
			if (!a_state.fallbackActivatedLogged && IsDiagnosticSlot2GuardMode()) {
				logger::debug(
					"[{}] {} activated triggerRva=0x{:X} blockedEvents={} blockedUniqueRvas={}",
					a_logPrefix,
					a_fallbackLabel,
					a_state.fallbackTriggerRva,
					a_state.rejectTotal,
					a_state.blockedCallerRvas.size());
				a_state.fallbackActivatedLogged = true;
			}
			return true;
		}

		return false;
	}

	void MaybeResetCallerFallbackOnGateTransition(
		CallerFallbackState& a_state,
		const bool a_gateSatisfied,
		const char* a_resetLogLine)
	{
		if (!a_state.gateActivePrevious && a_gateSatisfied) {
			ResetCallerFallbackState(a_state);
			if (IsDiagnosticSlot2GuardMode()) {
				logger::debug("{}", a_resetLogLine);
			}
		}
		a_state.gateActivePrevious = a_gateSatisfied;
	}

	bool ShouldApplySlot2Rewrite(const uint32_t a_callerRva)
	{
		// Selector for override map item (2): PS slot 2 rewrite path.
		return ShouldAllowCallerWithFallback(
			slot2FallbackState,
			kSlot2CallerAllowlistRvas,
			kEnableAutoBroadSlot2Fallback,
			kSlot2AutoFallbackRejectThreshold,
			true,
			"TB Override",
			"slot2 fallback",
			a_callerRva);
	}

	bool ShouldApplyDepthOverrideForCaller(const uint32_t a_callerRva)
	{
		// Selector for override map item (3): descriptor-scoped OM depth override.
		return ShouldAllowCallerWithFallback(
			depthOverrideFallbackState,
			kDepthOverrideCallerAllowlistRvas,
			kEnableAutoBroadDepthOverrideFallback,
			kDepthOverrideAutoFallbackRejectThreshold,
			true,
			"TB DepthOverride",
			"fallback",
			a_callerRva);
	}

	bool IsEngineHookFeatureGateSatisfied(const TerrainBlending& a_singleton)
	{
		if (!globals::game::isVR || !a_singleton.loaded || !a_singleton.settings.Enabled) {
			return false;
		}

		return !ShouldUseBlendedDepthSRV();
	}

	struct EngineHookPassGateState
	{
		bool gateSatisfied = false;
		bool inShadowmaskPhase = false;
		bool isUtility = false;
		bool isWhitelistedDescriptor = false;
		bool shouldApply = false;
	};

	EngineHookPassGateState EvaluateEngineHookPassGate(const TerrainBlending& a_singleton, RE::BSShader* a_shader, uint32_t a_descriptor)
	{
		EngineHookPassGateState state{};
		state.gateSatisfied = IsEngineHookFeatureGateSatisfied(a_singleton);
		state.inShadowmaskPhase = IsInRenderShadowmasksPhase();
		state.isUtility = a_shader && a_shader->shaderType.get() == RE::BSShader::Type::Utility;
		state.isWhitelistedDescriptor = IsShadowmaskDepthDescriptorWhitelisted(a_descriptor);
		state.shouldApply = state.gateSatisfied && state.inShadowmaskPhase && state.isUtility && state.isWhitelistedDescriptor;
		return state;
	}

	struct SlotOverrideResult
	{
		bool hasSrv = false;
		bool applied = false;
		bool alreadyBound = false;
	};

	using SlotRewriteGate = bool (*)(uint32_t);

	SlotOverrideResult ApplyPixelShaderSlotOverride(
		ID3D11DeviceContext* a_context,
		const uint32_t a_slot,
		ID3D11ShaderResourceView* a_overrideSrv,
		SlotRewriteGate a_rewriteGate,
		const uint32_t a_callerRva)
	{
		SlotOverrideResult result{};
		result.hasSrv = a_overrideSrv != nullptr;
		if (!result.hasSrv) {
			return result;
		}

		ID3D11ShaderResourceView* currentSrv = nullptr;
		a_context->PSGetShaderResources(a_slot, 1, &currentSrv);
		result.alreadyBound = currentSrv == a_overrideSrv;
		if (!result.alreadyBound) {
			const bool canRewrite = a_rewriteGate ? a_rewriteGate(a_callerRva) : true;
			if (canRewrite) {
				a_context->PSSetShaderResources(a_slot, 1, &a_overrideSrv);
				result.applied = true;
			}
		}

		if (currentSrv) {
			currentSrv->Release();
		}

		return result;
	}

	template <size_t N>
	void MaybeLogAllowlistHookActiveOnce(
		CallerFallbackState& a_state,
		const char* a_logPrefix,
		const char* a_countLabel,
		const char* a_allowlistLabel,
		const std::array<uint32_t, N>& a_allowlist,
		const uint64_t a_fallbackThreshold)
	{
		if (a_state.hookActiveLogged || !IsDiagnosticSlot2GuardMode()) {
			return;
		}

		std::ostringstream allowlist;
		for (size_t i = 0; i < a_allowlist.size(); i++) {
			if (i != 0) {
				allowlist << ", ";
			}
			allowlist << "0x" << std::uppercase << std::hex << a_allowlist[i];
		}

		logger::debug(
			"[{}] pass-specific hook active {}={} {}=[{}] fallbackThreshold={} fallbackActive={} blockedEvents={} blockedUniqueRvas={} triggerRva=0x{:X}",
			a_logPrefix,
			a_countLabel,
			a_allowlist.size(),
			a_allowlistLabel,
			allowlist.str(),
			a_fallbackThreshold,
			a_state.broadFallbackActive,
			a_state.rejectTotal,
			a_state.blockedCallerRvas.size(),
			a_state.fallbackTriggerRva);
		a_state.hookActiveLogged = true;
	}

	// Restores PS slots 17 and 2 to the SRVs that were bound before this shadowmask
	// This keeps override scope limited to the targeted pass and avoids leaking TB depth bindings into unrelated draws.
	void ReleaseEngineHookDepthOverride()
	{
		if (!engineHookTechniqueState.depthStateForced) {
			return;
		}

		auto* context = globals::d3d::context;
		if (context) {
			context->OMSetDepthStencilState(engineHookTechniqueState.previousDepthStencilState, engineHookTechniqueState.previousStencilRef);
		}

		if (engineHookTechniqueState.previousDepthStencilState) {
			engineHookTechniqueState.previousDepthStencilState->Release();
			engineHookTechniqueState.previousDepthStencilState = nullptr;
		}
		if (engineHookTechniqueState.forcedDepthStencilState) {
			engineHookTechniqueState.forcedDepthStencilState->Release();
			engineHookTechniqueState.forcedDepthStencilState = nullptr;
		}

		engineHookTechniqueState.previousStencilRef = 0;
		engineHookTechniqueState.depthStateForced = false;
	}

	void EnsureEngineHookDepthOverride(const uint32_t a_descriptor, const uint32_t a_callerRva)
	{
		// Descriptor-scoped safety gate: never apply this override outside 0x1062002.
		// This keeps the OM depth override local to the known problematic shadowmask path.
		if (a_descriptor != kShadowmaskDepthDescriptor1) {
			ReleaseEngineHookDepthOverride();
			return;
		}

		const bool allowCaller = ShouldApplyDepthOverrideForCaller(a_callerRva);
		if (!allowCaller) {
			ReleaseEngineHookDepthOverride();
			return;
		}

		auto* context = globals::d3d::context;
		auto* device = globals::d3d::device;
		if (!context || !device) {
			return;
		}

		// OM depth state can be clobbered between late engine hooks; re-assert here
		// so all four TB hook integration points keep consistent depth behavior.
		if (engineHookTechniqueState.depthStateForced) {
			if (!engineHookTechniqueState.forcedDepthStencilState) {
				ReleaseEngineHookDepthOverride();
			} else {
				UINT stencilRef = engineHookTechniqueState.previousStencilRef;
				ID3D11DepthStencilState* currentDepthStencilState = nullptr;
				context->OMGetDepthStencilState(&currentDepthStencilState, &stencilRef);
				if (currentDepthStencilState) {
					currentDepthStencilState->Release();
				}
				context->OMSetDepthStencilState(engineHookTechniqueState.forcedDepthStencilState, stencilRef);
				return;
			}
		}

		ID3D11DepthStencilState* currentDepthStencilState = nullptr;
		UINT currentStencilRef = 0;
		context->OMGetDepthStencilState(&currentDepthStencilState, &currentStencilRef);
		if (!currentDepthStencilState) {
			return;
		}

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		currentDepthStencilState->GetDesc(&depthStencilDesc);

		if (!depthStencilDesc.DepthEnable || depthStencilDesc.DepthFunc == D3D11_COMPARISON_ALWAYS) {
			currentDepthStencilState->Release();
			return;
		}

		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

		ID3D11DepthStencilState* forcedDepthStencilState = nullptr;
		const HRESULT hr = device->CreateDepthStencilState(&depthStencilDesc, &forcedDepthStencilState);
		if (FAILED(hr) || !forcedDepthStencilState) {
			currentDepthStencilState->Release();
			return;
		}

		engineHookTechniqueState.previousDepthStencilState = currentDepthStencilState;
		engineHookTechniqueState.previousStencilRef = currentStencilRef;
		engineHookTechniqueState.forcedDepthStencilState = forcedDepthStencilState;
		engineHookTechniqueState.depthStateForced = true;

		context->OMSetDepthStencilState(forcedDepthStencilState, currentStencilRef);
	}

	void ReleaseEngineHookTechniqueOverride()
	{
		ReleaseEngineHookDepthOverride();

		if (!engineHookTechniqueState.active) {
			return;
		}

		auto* context = globals::d3d::context;
		if (context) {
			context->PSSetShaderResources(17, 1, &engineHookTechniqueState.previousObbSrv);
			context->PSSetShaderResources(2, 1, &engineHookTechniqueState.previousShadowmaskSrv);
		}

		if (engineHookTechniqueState.previousObbSrv) {
			engineHookTechniqueState.previousObbSrv->Release();
			engineHookTechniqueState.previousObbSrv = nullptr;
		}
		if (engineHookTechniqueState.previousShadowmaskSrv) {
			engineHookTechniqueState.previousShadowmaskSrv->Release();
			engineHookTechniqueState.previousShadowmaskSrv = nullptr;
		}

		engineHookTechniqueState.active = false;
	}
}

void TerrainBlending::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enable"), "Enable Terrain Blending"), (bool*)&settings.Enabled);

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_tooltip"), "Enable seamless blending between terrain and objects."));
	}
}

void TerrainBlending::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TerrainBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainBlending::OnBeginTechnique(RE::BSShader* a_shader, uint32_t a_pixelDescriptor, uint32_t a_callerRva)
{
	const auto gateState = EvaluateEngineHookPassGate(*this, a_shader, a_pixelDescriptor);
	// Keep fallback state bounded to the same TB gate lifecycle as slot2 rewrite.
	MaybeResetCallerFallbackOnGateTransition(slot2FallbackState, gateState.gateSatisfied, "[TB Override] slot2 fallback reset on TB/depth-culling off->on");
	MaybeResetCallerFallbackOnGateTransition(depthOverrideFallbackState, gateState.gateSatisfied, "[TB DepthOverride] fallback reset on TB/depth-culling off->on");

	if (gateState.shouldApply) {
		MaybeLogAllowlistHookActiveOnce(
			slot2FallbackState,
			"TB Override",
			"slot2AllowlistCount",
			"slot2AllowlistRvas",
			kSlot2CallerAllowlistRvas,
			kSlot2AutoFallbackRejectThreshold);
		// Depth-override logging is restricted to the descriptor-scoped path.
		if (a_pixelDescriptor == kShadowmaskDepthDescriptor1) {
			MaybeLogAllowlistHookActiveOnce(
				depthOverrideFallbackState,
				"TB DepthOverride",
				"allowlistCount",
				"allowlistRvas",
				kDepthOverrideCallerAllowlistRvas,
				kDepthOverrideAutoFallbackRejectThreshold);
		}
	}

	if (!gateState.shouldApply) {
		ReleaseEngineHookTechniqueOverride();
		return;
	}

	auto* context = globals::d3d::context;
	if (!context) {
		ReleaseEngineHookTechniqueOverride();
		return;
	}

	// Integration point 1/4 for override-map item (3): apply descriptor-scoped OM depth override.
	EnsureEngineHookDepthOverride(a_pixelDescriptor, a_callerRva);

	if (!engineHookTechniqueState.active) {
		context->PSGetShaderResources(17, 1, &engineHookTechniqueState.previousObbSrv);
		context->PSGetShaderResources(2, 1, &engineHookTechniqueState.previousShadowmaskSrv);
		engineHookTechniqueState.active = true;
	}

	auto* obbOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	// Use R32 depth for shadowmask slot2 override to reduce edge instability during motion.
	auto* shadowmaskOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	// Override map items (1) and (2).
	ApplyPixelShaderSlotOverride(context, 17u, obbOverrideSrv, nullptr, 0u);
	ApplyPixelShaderSlotOverride(context, 2u, shadowmaskOverrideSrv, &ShouldApplySlot2Rewrite, a_callerRva);
}

void TerrainBlending::OnShadowmaskPhaseEnd()
{
	ReleaseEngineHookTechniqueOverride();
}

void TerrainBlending::OnUtilitySetupGeometry(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags, uint32_t a_callerRva)
{
	(void)a_pass;
	(void)a_renderFlags;

	auto* state = globals::state;
	const uint32_t descriptor = state ? state->currentPixelDescriptor : 0u;

	const auto gateState = EvaluateEngineHookPassGate(*this, a_shader, descriptor);
	if (!gateState.shouldApply) {
		return;
	}

	auto* context = globals::d3d::context;
	if (!context) {
		return;
	}

	// Integration point 2/4 for override-map item (3): re-assert after Utility geometry setup mutates OM state.
	EnsureEngineHookDepthOverride(descriptor, a_callerRva);

	auto* obbOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	auto* shadowmaskOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	ApplyPixelShaderSlotOverride(context, 17u, obbOverrideSrv, nullptr, 0u);
	ApplyPixelShaderSlotOverride(context, 2u, shadowmaskOverrideSrv, &ShouldApplySlot2Rewrite, a_callerRva);
}

void TerrainBlending::OnShaderPropertySetupGeometry(RE::BSShaderProperty* a_shaderProperty, RE::BSGeometry* a_geometry, bool a_result, uint32_t a_callerRva)
{
	(void)a_shaderProperty;
	(void)a_geometry;
	(void)a_result;

	auto* state = globals::state;
	RE::BSShader* shader = state ? state->currentShader : nullptr;
	const uint32_t descriptor = state ? state->currentPixelDescriptor : 0u;

	const auto gateState = EvaluateEngineHookPassGate(*this, shader, descriptor);
	if (!gateState.shouldApply) {
		return;
	}

	auto* context = globals::d3d::context;
	if (!context) {
		return;
	}

	// Integration point 3/4 for override-map item (3): re-assert after material/property setup.
	EnsureEngineHookDepthOverride(descriptor, a_callerRva);

	auto* shadowmaskOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	ApplyPixelShaderSlotOverride(context, 2u, shadowmaskOverrideSrv, &ShouldApplySlot2Rewrite, a_callerRva);
}

void TerrainBlending::OnSetDirtyStates(bool a_isCompute, uint32_t a_callerRva)
{
	// Slot2 clobber was traced to BSGraphics::SetDirtyStates.
	// Integration point 4/4: re-assert override-map item (3), plus SRV overrides
	// for map items (1) and (2), under strict TB pass gates instead of global D3D interception.
	if (a_isCompute) {
		return;
	}

	auto* state = globals::state;
	RE::BSShader* shader = state ? state->currentShader : nullptr;
	const uint32_t descriptor = state ? state->currentPixelDescriptor : 0u;

	const auto gateState = EvaluateEngineHookPassGate(*this, shader, descriptor);
	if (!gateState.shouldApply) {
		return;
	}

	auto* context = globals::d3d::context;
	if (!context) {
		return;
	}

	EnsureEngineHookDepthOverride(descriptor, a_callerRva);

	auto* obbOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	auto* shadowmaskOverrideSrv = Util::GetCurrentSceneDepthSRV(false);
	ApplyPixelShaderSlotOverride(context, 17u, obbOverrideSrv, nullptr, 0u);
	ApplyPixelShaderSlotOverride(context, 2u, shadowmaskOverrideSrv, &ShouldApplySlot2Rewrite, a_callerRva);
}

ID3D11VertexShader* TerrainBlending::GetTerrainVertexShader()
{
	if (!terrainVertexShader) {
		logger::debug("Compiling Utility.hlsl");
		terrainVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" } }, "vs_5_0");
	}
	return terrainVertexShader;
}

ID3D11VertexShader* TerrainBlending::GetTerrainOffsetVertexShader()
{
	if (!terrainOffsetVertexShader) {
		logger::debug("Compiling Utility.hlsl");
		terrainOffsetVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" }, { "OFFSET_DEPTH", "" } }, "vs_5_0");
	}
	return terrainOffsetVertexShader;
}

ID3D11ComputeShader* TerrainBlending::GetDepthBlendShader()
{
	if (!depthBlendShader) {
		logger::debug("Compiling DepthBlend.hlsl");
		depthBlendShader = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\TerrainBlending\\DepthBlend.hlsl", {}, "cs_5_0");
	}
	return depthBlendShader;
}

void TerrainBlending::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc;
		mainDepth.texture->GetDesc(&texDesc);
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, NULL, &terrainDepth.texture));
		Util::SetResourceName(terrainDepth.texture, "TerrainBlending::TerrainDepth");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		mainDepth.depthSRV->GetDesc(&srvDesc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(terrainDepth.texture, &srvDesc, &terrainDepth.depthSRV));
		Util::SetResourceName(terrainDepth.depthSRV, "TerrainBlending::TerrainDepth SRV");

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		mainDepth.views[0]->GetDesc(&dsvDesc);
		DX::ThrowIfFailed(device->CreateDepthStencilView(terrainDepth.texture, &dsvDesc, &terrainDepth.views[0]));
		Util::SetResourceName(terrainDepth.views[0], "TerrainBlending::TerrainDepth DSV");
	}

	{
		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		blendedDepthTexture = new Texture2D(texDesc, "TerrainBlending::BlendedDepth");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		main.SRV->GetDesc(&srvDesc);
		srvDesc.Format = texDesc.Format;
		blendedDepthTexture->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		main.UAV->GetDesc(&uavDesc);
		uavDesc.Format = texDesc.Format;
		blendedDepthTexture->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R16_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		blendedDepthTexture16 = new Texture2D(texDesc, "TerrainBlending::BlendedDepth16");
		blendedDepthTexture16->CreateSRV(srvDesc);
		blendedDepthTexture16->CreateUAV(uavDesc);

		// R32_FLOAT snapshot of main depth written by DepthBlend CS; replaces the per-frame
		// CopyResource(terrainDepth <- mainDepth) that was needed for terrain shader slot 55.
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		mainDepthCopy = new Texture2D(texDesc, "TerrainBlending::MainDepthCopy");
		mainDepthCopy->CreateSRV(srvDesc);
		mainDepthCopy->CreateUAV(uavDesc);

		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		depthSRVBackup = mainDepth.depthSRV;

		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		prepassSRVBackup = zPrepassCopy.depthSRV;
	}

	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthStencilDesc.StencilEnable = false;
		DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, &terrainDepthStencilState));
		Util::SetResourceName(terrainDepthStencilState, "TerrainBlending::DepthStencilState");
	}
}

void TerrainBlending::PostPostLoad()
{
	Hooks::Install();
}

void TerrainBlending::DataLoaded()
{
	auto bEnableLandFade = RE::GetINISetting("bEnableLandFade:Display");
	bEnableLandFade->data.b = false;
}

void TerrainBlending::TerrainShaderHacks()
{
	if (renderTerrainDepth) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;
		if (renderAltTerrain) {
			auto dsv = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			context->VSSetShader(GetTerrainOffsetVertexShader(), NULL, NULL);
		} else {
			auto dsv = terrainDepth.views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			auto shadowState = globals::game::shadowState;
			GET_INSTANCE_MEMBER(currentVertexShader, shadowState)
			context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);
		}
		renderAltTerrain = !renderAltTerrain;
	}
}

void TerrainBlending::ResetDepth()
{
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Reset Depth");
	auto context = globals::d3d::context;

	auto dsv = terrainDepth.views[0];
	context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0u);
}

void TerrainBlending::ResetTerrainDepth()
{
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Reset Terrain Depth");
	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("Terrain Blending - Reset Terrain Depth");

	auto context = globals::d3d::context;

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	auto currentVertexShader = *globals::game::currentVertexShader;
	context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void TerrainBlending::BlendPrepassDepths()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Blend Prepass Depths");
	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("Terrain Blending - Blend Prepass Depths");

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Depth Blend CS");

		ID3D11ShaderResourceView* views[2] = { depthSRVBackup, terrainDepth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		// u0=blendedDepth(R32), u1=blendedDepth16(R16), u2=mainDepthCopy(R32) written inline
		ID3D11UnorderedAccessView* uavs[3] = { blendedDepthTexture->uav.get(), blendedDepthTexture16->uav.get(), mainDepthCopy->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetDepthBlendShader(), nullptr, 0);

		globals::profiler->BeginPass("TerrainBlending::DepthBlend");
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		globals::profiler->EndPass();
	}

	ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[3] = { nullptr, nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	// CopyResource(terrainDepth <- mainDepth) eliminated: main depth is now written
	// directly into mainDepthCopy (u2) by the CS above, saving a full-stereo D24S8 copy.

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void TerrainBlending::ClearShaderCache()
{
	if (terrainVertexShader) {
		terrainVertexShader->Release();
		terrainVertexShader = nullptr;
	}
	if (terrainOffsetVertexShader) {
		terrainOffsetVertexShader->Release();
		terrainOffsetVertexShader = nullptr;
	}
	if (depthBlendShader) {
		depthBlendShader->Release();
		depthBlendShader = nullptr;
	}
}

void TerrainBlending::Hooks::Main_RenderDepth::thunk(bool a1, bool a2)
{
	ZoneScopedS(8);

	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;
	auto renderer = globals::game::renderer;

	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	globals::game::graphicsState->SetCameraData(RE::Main::WorldRootCamera(), 1);

	singleton.averageEyePosition = Util::GetAverageEyePosition();

	const bool tbActive = shaderCache->IsEnabled() && singleton.settings.Enabled;
	const bool useBlendedDepthSRV = tbActive && ShouldUseBlendedDepthSRV();

	if (tbActive) {
		if (useBlendedDepthSRV) {
			mainDepth.depthSRV = singleton.blendedDepthTexture->srv.get();
			zPrepassCopy.depthSRV = singleton.blendedDepthTexture->srv.get();
		} else {
			mainDepth.depthSRV = singleton.depthSRVBackup;
			zPrepassCopy.depthSRV = singleton.prepassSRVBackup;
		}

		singleton.renderDepth = true;
		singleton.ResetDepth();

		{
			ZoneScopedN("Terrain Depth - Game Render");
			func(a1, a2);
		}

		singleton.renderDepth = false;

		if (singleton.renderTerrainDepth) {
			singleton.renderTerrainDepth = false;
			singleton.ResetTerrainDepth();
		}

		singleton.BlendPrepassDepths();
	} else {
		mainDepth.depthSRV = singleton.depthSRVBackup;
		zPrepassCopy.depthSRV = singleton.prepassSRVBackup;

		{
			ZoneScopedN("Terrain Depth - Game Render");
			func(a1, a2);
		}
	}
}

void TerrainBlending::Hooks::BSBatchRenderer__RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;

	if (shaderCache->IsEnabled() && singleton.settings.Enabled) {
		if (singleton.renderDepth) {
			// Entering or exiting terrain depth section
			bool inTerrain = a_pass->shaderProperty && a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape);

			if (inTerrain && a_pass->geometry) {
				if ((a_pass->geometry->worldBound.center.GetDistance(singleton.averageEyePosition) - a_pass->geometry->worldBound.radius) > 1024.0f) {
					inTerrain = false;
				}
			}

			if (singleton.renderTerrainDepth != inTerrain) {
				if (!inTerrain)
					singleton.ResetTerrainDepth();
				singleton.renderTerrainDepth = inTerrain;
			}

			if (inTerrain) {
				func(a_pass, a_technique, a_alphaTest, a_renderFlags);  // Run terrain twice
			}
		} else if (globals::state->inWorld) {
			if (auto shaderProperty = a_pass->shaderProperty) {
				if (a_pass->shader->shaderType.get() == RE::BSShader::Type::Lighting) {
					if (shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
						RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
						singleton.terrainRenderPasses.push_back(call);
						return;
					}

					// Detect meshes which should not get terrain blending using an unused flag (kNoTransparencyMultiSample)
					if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kNoTransparencyMultiSample)) {
						RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
						singleton.renderPasses.push_back(call);
						return;
					}
				}
			}
		}
	}
	func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

void TerrainBlending::Hooks::BSUtilityShader_SetupGeometry::thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags)
{
	const auto callerRva = ToModuleRva(_ReturnAddress());
	func(a_shader, a_pass, a_renderFlags);
	globals::features::terrainBlending.OnUtilitySetupGeometry(a_shader, a_pass, a_renderFlags, callerRva);
}

bool TerrainBlending::Hooks::BSShaderProperty_SetupGeometry::thunk(RE::BSShaderProperty* a_shaderProperty, RE::BSGeometry* a_geometry)
{
	const auto callerRva = ToModuleRva(_ReturnAddress());
	const bool result = func(a_shaderProperty, a_geometry);
	globals::features::terrainBlending.OnShaderPropertySetupGeometry(a_shaderProperty, a_geometry, result, callerRva);
	return result;
}

void TerrainBlending::Hooks::Main_RenderShadowmasks::thunk(bool a1)
{
	renderShadowmasksPhaseDepth.fetch_add(1, std::memory_order_relaxed);
	func(a1);
	if (renderShadowmasksPhaseDepth.fetch_sub(1, std::memory_order_relaxed) == 1) {
		globals::features::terrainBlending.OnShadowmaskPhaseEnd();
	}
}

bool TerrainBlending::Hooks::BSShader_BeginTechnique::thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader)
{
	const auto callerRva = ToModuleRva(_ReturnAddress());
	bool result = func(shader, vertexDescriptor, pixelDescriptor, skipPixelShader);
	globals::features::terrainBlending.OnBeginTechnique(shader, pixelDescriptor, callerRva);
	return result;
}

void TerrainBlending::Hooks::BSGraphics_SetDirtyStates::thunk(bool isCompute)
{
	const auto callerRva = ToModuleRva(_ReturnAddress());
	func(isCompute);
	globals::features::terrainBlending.OnSetDirtyStates(isCompute, callerRva);
}

void TerrainBlending::RenderTerrainBlendingPasses()
{
	ZoneScoped;

	if (!settings.Enabled) {
		renderDepth = false;
		renderTerrainDepth = false;
		renderAltTerrain = false;
		terrainRenderPasses.clear();
		renderPasses.clear();
		auto renderer = globals::game::renderer;
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		mainDepth.depthSRV = depthSRVBackup;
		zPrepassCopy.depthSRV = prepassSRVBackup;
		return;
	}

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto shadowState = globals::game::shadowState;
	auto stateUpdateFlags = globals::game::stateUpdateFlags;

	// Slot 55: main depth snapshot used to measure surface-to-lowest-depth distance.
	// R32_FLOAT copy written inline by DepthBlend CS; safe to read here because
	// mainDepthCopy is not written during the main rendering pass.
	auto mainDepthSRV = mainDepthCopy->srv.get();
	context->PSSetShaderResources(55, 1, &mainDepthSRV);

	const uint64_t terrainPassCount = static_cast<uint64_t>(terrainRenderPasses.size());
	const uint64_t noBlendPassCount = static_cast<uint64_t>(renderPasses.size());

	if (terrainPassCount != 0 || noBlendPassCount != 0) {
		TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Render Passes");
		if (globals::state->frameAnnotations)
			globals::state->BeginPerfEvent("Terrain Blending - Render Passes");

		GET_INSTANCE_MEMBER(alphaBlendMode, shadowState)
		GET_INSTANCE_MEMBER(alphaBlendWriteMode, shadowState)
		GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState)

		// Reset alpha write and enable alpha blending
		alphaBlendWriteMode = 1;
		alphaBlendMode = 1;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Enable rendering for depth below the surface
		context->OMSetDepthStencilState(terrainDepthStencilState, 0xFF);

		for (auto& renderPass : terrainRenderPasses)
			Hooks::BSBatchRenderer__RenderPassImmediately::func(renderPass.a_pass, renderPass.a_technique, renderPass.a_alphaTest, renderPass.a_renderFlags);

		// Reset alpha blending
		alphaBlendMode = 0;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Reset depth testing
		depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestEqual;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

		for (auto& renderPass : renderPasses)
			Hooks::BSBatchRenderer__RenderPassImmediately::func(renderPass.a_pass, renderPass.a_technique, renderPass.a_alphaTest, renderPass.a_renderFlags);

		terrainRenderPasses.clear();
		renderPasses.clear();

		if (globals::state->frameAnnotations)
			globals::state->EndPerfEvent();
	}

	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	mainDepth.depthSRV = depthSRVBackup;
}
#undef I18N_KEY_PREFIX
