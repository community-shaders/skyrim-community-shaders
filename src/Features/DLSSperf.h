#pragma once

// ============================================================================
// DLSSperf — render-target size hook + post-processing interception
// ============================================================================
//
// Sub-feature of DlssEnhancer.  Hooks BSOpenVR::GetRenderTargetSize so all
// engine render targets are allocated at a small RenderRes while DLSS writes
// its output to a private DisplayRes testTexture.
//
//  Benefits:
//   - VRAM and bandwidth savings proportional to the quality-mode scale ratio.
//   - UpscaleRT is no longer needed.
//   - Game menus are no longer occluded by the upscaler output.
//
//  Current limitation:
//   Post-processing still runs on 1k kMAIN via a bilinear downsample of
//   testTexture.  Performance is good and visual loss is minimal.  Once the
//   post chain is rewritten to consume testTexture natively, the downsample
//   can be removed.
//
// ============================================================================

#include <functional>

struct DLSSperf
{
	void SetupResources();
	void DrawSettings();

	// Phase 1: standalone test texture that receives Upscaling output instead of kMAIN.
	// Returns nullptr when not ready.
	ID3D11Texture2D* GetTestTexture() const { return testTexture.get(); }
	ID3D11ShaderResourceView* GetTestTextureSRV() const { return testTextureSRV.get(); }
	ID3D11UnorderedAccessView* GetTestTextureUAV() const { return testTextureUAV.get(); }
	ID3D11Texture2D* GetRefraTempTex() const { return refraTempTex.get(); }
	ID3D11ShaderResourceView* GetRefraTempSRV() const { return refraTempSRV.get(); }

	// Phase 2: resolution hook status
	bool IsHookActive() const { return hookActive; }
	bool IsPostInterceptActive() const { return postInterceptActive; }
	bool IsPostChainDone() const { return postChainDone; }
	void ClearPostChainDone() { postChainDone = false; }
	uint32_t GetDisplayEyeWidth() const { return displayEyeWidth; }
	uint32_t GetDisplayEyeHeight() const { return displayEyeHeight; }
	uint32_t GetRenderEyeWidth() const { return renderEyeWidth; }
	uint32_t GetRenderEyeHeight() const { return renderEyeHeight; }

	// Phase 3: real HMD display resolution in SBS format (e.g. 3072×1632)
	// Used by Upscaling pipeline to override polluted screenSize (which equals RenderRes after hook)
	float2 GetDisplayScreenSize() const
	{
		return { static_cast<float>(displayEyeWidth * 2), static_cast<float>(displayEyeHeight) };
	}

	// Phase 2: called from BSShaderRenderTargets_Create::thunk (before func())
	// where BSOpenVR is guaranteed to be available
	void InstallRenderTargetSizeHook();

	// Hybrid Post: tonemap interception via IS shader hooks
	// Call BeginPostIntercept() before func(), EndPostIntercept() after.
	void BeginPostIntercept();
	void EndPostIntercept();

	// Downscale testTexture (3k AA'd DLSS output) → kMAIN (1k)
	// so the HDR pyramid builds from anti-aliased content instead of raw 1k render.
	// Only kMAIN: no-refra reads kMAIN directly; with-refra engine copies kMAIN→kMAIN_COPY.
	void DownscaleToKMain();

	// Post hybrid entry point: called from Upscaling's Main_PostProcessing::thunk.
	// Wraps the engine Post chain with DLSSperf's two-layer struct swap.
	bool ShouldHandlePost() const { return hookActive && testTexture; }
	void HandlePostProcessing(const std::function<void()>& enginePost);

	// Fake 3k DepthStencil for Post pass DS swap
	ID3D11DepthStencilView* GetFakeDSV() const { return fakeDSV.get(); }

private:
	// Phase 1
	winrt::com_ptr<ID3D11Texture2D> testTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> testTextureSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView> testTextureUAV;

	// Phase 2: resolution hook state
	bool hookActive = false;

	// Post intercept phase flag: when true, VP post-correction is skipped
	// so enlarged kTEMP/kTOTAL get correct 3k VP from engine.
	bool postInterceptActive = false;

	// Post-chain-done flag: set true after EndPostIntercept, cleared at
	// PlayerView end by PlayerViewRender_Hook.  When true, UpdateViewPort
	// hook expands VP to displayRes so draws after the Post chain
	// (UI合成, scene fade, submit prep) use correct 3k VP.
	bool postChainDone = false;

	uint32_t displayEyeWidth = 0;
	uint32_t displayEyeHeight = 0;
	uint32_t renderEyeWidth = 0;
	uint32_t renderEyeHeight = 0;

	// Phase 2: vtable hook for BSOpenVR::GetRenderTargetSize (vfunc 0x12)
	struct GetRenderTargetSize_Hook
	{
		static void thunk(RE::BSOpenVR* a_this, uint32_t* a_width, uint32_t* a_height);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// IS shader hook: ISHDRTonemapBlendCinematic (Render vfunc 0x1 on vtable[3])
	// Chains after FrameAnnotations (if active). Swaps kMAIN SRV + kMAIN DS before
	// tonemap, restores after.
	struct TonemapRender_Hook
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	bool tonemapHookInstalled = false;

	// IS shader hook: ISRefraction (Render vfunc 0x1 on vtable[3])
	// Replay DrawIndexed: func() runs 1k refraction normally, then replays 3k draw with sticky D3D state.
	struct RefractionRender_Hook
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	bool refractionHookInstalled = false;

	// UI pass hook: FinishAccumulatingDispatch (vfunc 0x2A on BSShaderAccumulator)
	// When renderMode==24 (UI pass), swaps KMAIN DS → fakeDS so 3k kMENUBG gets 3k depth.
	struct UIPassDispatch_Hook
	{
		static void thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	bool uiPassHookInstalled = false;

	// PlayerView end hook: Main_RenderPlayerView (REL 35560/36559)
	// Clears postChainDone after the entire VR pipeline (World→Post→UI→Submit)
	// so Present-前 UI chain and next frame use normal VP compression.
	struct PlayerViewRender_Hook
	{
		static void thunk(void* a1, bool a2, bool a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	bool playerViewHookInstalled = false;

	// Refraction: 3k temp texture (copy of testTexture) for ISRefraction input
	winrt::com_ptr<ID3D11Texture2D> refraTempTex;
	winrt::com_ptr<ID3D11ShaderResourceView> refraTempSRV;
	// Refraction: RTV for testTexture (ISRefraction 3k output target)
	winrt::com_ptr<ID3D11RenderTargetView> testTextureRTV;

	// Two-layer swap: saved pointers for restore
	// Outer layer (BeginPost/EndPost): kMAIN_COPY DS + SRV
	ID3D11DepthStencilView* savedKMainCopyViews[8] = {};
	ID3D11DepthStencilView* savedKMainCopyReadOnlyViews[8] = {};
	ID3D11ShaderResourceView* savedKMainCopySRV = nullptr;
	// Inner layer (tonemap hook): kMAIN DS + kMAIN SRV
	ID3D11DepthStencilView* savedKMainViews[8] = {};
	ID3D11DepthStencilView* savedKMainReadOnlyViews[8] = {};
	ID3D11ShaderResourceView* savedKMainSRV = nullptr;

	// Fake 3k DepthStencil (DisplayRes, same format as engine kMAIN DS)
	winrt::com_ptr<ID3D11Texture2D> fakeDS;
	winrt::com_ptr<ID3D11DepthStencilView> fakeDSV;

	// Downscale pass: Box 3×3 downscale testTexture (3k) → kMAIN (1k)
	winrt::com_ptr<ID3D11PixelShader> bilinearCopyPS;
	winrt::com_ptr<ID3D11VertexShader> bilinearCopyVS;
	winrt::com_ptr<ID3D11SamplerState> linearSampler;
};