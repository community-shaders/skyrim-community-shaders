#pragma once

// TAA Reordering for VR DLSS Viewport Scaling (Post-Conductor DLSS)
//
// PureDark's approach: DLSS is evaluated AFTER BSImagespaceShader::Render
// completes (which includes the conductor + Phase 5 TAA + DRS).
//
// Flow:
//   1. BSImagespaceShaderHook wraps the call at 0x132C827:
//      func() runs → conductor executes Phase 2A → Phase 5 (TAA + DRS)
//      a. ExecutePassHook captures Phase 2A output to g_postPPCopy
//   2. After func() returns in BSImagespaceShaderHook:
//      a. Gets submit texture from bound RT (now has TAA-upscaled content)
//      b. Evaluates DLSS on g_postPPCopy (post-PP intermediate)
//      c. Pastes DLSS center from g_postPPCopy onto submit texture
//   3. Engine continues: Orchestrator → Scaleform Display (UI) → Submit
//   4. Lock DRS + UpdateCameraData (in Main_PostProcessing::thunk after func())
//
// Both DLSS and TAA get Phase 2A's PP applied:
//   - TAA: naturally (Phase 2A runs before Phase 5 in conductor)
//   - DLSS: processes the Phase 2A output copy (g_postPPCopy)
//
// All RVAs are VR-specific (SkyrimVR.exe).

#include <Windows.h>
#include <d3d11.h>
#include <intrin.h>
#include <winrt/base.h>

struct Upscaling;

namespace TAAReorder
{
	// ─── Function pointer types ───
	using ExecutePass_t = void (*)(void* manager, void* passObj, int srcTech, int dstTech, void* extraData, uint8_t flag);
	using BSOpenVRSubmit_t = void (*)(void* thisPtr, void* textureHandle);

	// ─── Resolved global data pointers ───
	inline uintptr_t* g_pRendererSingleton = nullptr;
	inline bool g_initialized = false;

	// ─── Diagnostics (rate-limited logging) ───
	inline int g_diagCounter = 0;
	inline constexpr int DIAG_INTERVAL = 300;

	// ─── Per-frame sequence counter (for verifying call ordering) ───
	inline int g_frameSeqCounter = 0;

	// ─── ExecutePass hook (conductor interposition) ───
	// RVA: 0x012D2540 — called by the conductor for each render pass.
	// Copies Phase 2A output RT to g_postPPCopy for DLSS to process.
	struct ExecutePassHook
	{
		static void thunk(void* manager, void* passObj, int srcTech, int dstTech, void* extraData, uint8_t flag);
		static inline ExecutePass_t func = nullptr;
	};

	// ─── BSImagespaceShader hook (DLSS eval + paste after pipeline completes) ───
	// RVA: 0x132C827 — write_thunk_call wrapping BSImagespaceShader::Render.
	// This is the OUTER call that encompasses the conductor + Phase 5 (TAA+DRS).
	// After func() returns: submit texture has TAA-upscaled content.
	// We evaluate DLSS on g_postPPCopy and paste the center onto submit texture.
	// (Matches PureDark's BSImagespaceShader_Hook_VR)
	struct BSImagespaceShaderHook
	{
		static void thunk(void* a_this, uint64_t a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ─── Post-processing conductor call hook (pass-through, tracking only) ───
	// RVA: 0x1325086 — inner conductor call inside BSImagespaceShader::Render.
	// Only used for g_insideConductor tracking.
	struct ConductorCallHook
	{
		static void thunk(void* a1, void* a2, void* a3, void* a4);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ─── Post-PP copy (Phase 2A output, DLSS color source) ───
	// After Phase 2A completes, ExecutePassHook copies the bound RT here.
	// BSImagespaceShaderHook passes this to Upscale() as colorSourceOverride.
	// After DLSS, FinalizePerEyeOutputs writes DLSS center back into this texture.
	inline winrt::com_ptr<ID3D11Texture2D> g_postPPCopy;
	inline winrt::com_ptr<ID3D11ShaderResourceView> g_postPPCopySRV;
	inline bool g_postPPReady = false;

	// ─── DLSS evaluation complete flag ───
	// Set after BSImagespaceShaderHook evaluates DLSS on g_postPPCopy.
	// Used to gate the DLSS center paste step.
	inline bool g_dlssReady = false;

	// ─── DLSS paste complete flag ───
	// Set after ConductorCallHook pastes DLSS center onto submit texture.
	inline bool g_dlssPasteComplete = false;

	// ─── Phase 5 tracking ───
	inline bool g_phase5Complete = false;

	// ─── Conductor state tracking ───
	inline bool g_insideConductor = false;
	inline int g_bsHookCallCount = 0;

	// ─── RGB-only blend state (may be useful for future feathering) ───
	inline winrt::com_ptr<ID3D11BlendState> g_rgbOnlyBlendState;

	// ─── Stencil state for HAM-aware compositing ───
	// DepthEnable=false, StencilEnable=true, StencilFunc=EQUAL, StencilRef=0.
	// Only writes to pixels where stencil==0 (visible, non-HAM pixels).
	// Matches PureDark's approach in Evaluate()/RenderTexture().
	inline winrt::com_ptr<ID3D11DepthStencilState> g_hamStencilState;

	// ─── Cached UAV for submit texture (ClearHMDMask + ForceAlpha on submit after DLSS paste) ───
	inline winrt::com_ptr<ID3D11UnorderedAccessView> g_submitTexUAV;
	inline ID3D11Texture2D* g_submitTexUAVOwner = nullptr;  // track which texture the UAV belongs to

	// ─── ForceAlpha compute shader (sets alpha=1.0 to fix Scaleform UI rendering) ───
	inline winrt::com_ptr<ID3D11ComputeShader> g_forceAlphaCS;

	// ─── Setter hook: Setter A (Force TAA) ───
	// RVA: 0x005C8EE0 — unconditional TAA enable.
	// Pass-through (we want TAA to run natively).
	struct ForceTAASetter
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ─── Setter hook: Setter B (TAA State Machine) ───
	// RVA: 0x005C8F10 — conditional TAA enable/disable.
	// Pass-through (we want TAA to run natively).
	struct TAAStateMachine
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ─── Depth/stencil registration hook ───
	// RVA: 0x00DC79D0 — registers depth/stencil targets in the RT manager (+0x1388).
	// Separate path from color RTs (registered via sub_417980 at +0x1350).
	// Hook intercepts registration to log descriptor layout and scale dimensions
	// for slots 0, 1, 7 to match display resolution (fixes HAM not being upscaled).
	using RegisterDepthStencil_t = void (*)(void* manager, uint32_t slot, void* desc);
	struct DepthStencilRegHook
	{
		static void thunk(void* manager, uint32_t slot, void* desc);
		static inline RegisterDepthStencil_t func = nullptr;
	};

	// ─── BSOpenVR::Submit hook (VR frame submission interception) ───
	// RVA: 0x00C53920 — BSOpenVR::Submit, vtable[3].
	// Diagnostic logging only.
	struct SubmitHook
	{
		static void thunk(void* thisPtr, void* textureHandle);
		static inline BSOpenVRSubmit_t func = nullptr;
	};

	// Check if TAA reordering should be active based on current settings
	bool ShouldReorderTAA();

	// Ensure g_postPPCopy matches the source texture dimensions/format
	void EnsurePostPPCopy(ID3D11Texture2D* sourceTex);

	// Helper: draw fullscreen format-converting copy (Load-based, 1:1 pixel copy).
	void DrawFullscreenCopy(ID3D11ShaderResourceView* srcSRV, ID3D11RenderTargetView* dstRTV,
		float vpX, float vpY, float vpW, float vpH);

	// Install hooks that must be in place before renderer initialization (depth/stencil reg).
	// Call from Upscaling::Load() (VR only).
	void InitEarly();

	// Initialize all pointers and install hooks. Call once from PostPostLoad (VR only).
	void Init();
}
