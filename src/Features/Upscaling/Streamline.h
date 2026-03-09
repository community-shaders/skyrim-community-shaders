#pragma once

#include "../../Buffer.h"
#include "../../State.h"

#include <d3d11_4.h>
#include <d3d12.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_matrix_helpers.h>
#include <sl_version.h>
#pragma warning(pop)

class Streamline
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";

	Streamline() = default;

	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;
	bool triedInitialization = false;

	eastl::vector<sl::Feature> features;

	enum Features
	{
		kNone = 0,
		kDLSS = 1 << 0,
		kDLSS_RR = 1 << 1
	} loadedFeatures;

	bool d3d12Mode = false;

	sl::ViewportHandle viewport{ 0 };
	sl::ViewportHandle viewportRight{ 1 };
	static constexpr uint32_t MAX_RESOLUTION = 8192;
	HMODULE interposer = NULL;

	// SL Interposer Functions
	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slAllocateResources* slAllocateResources{};
	PFun_slFreeResources* slFreeResources{};
	PFun_slSetTag* slSetTag{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureVersion* slGetFeatureVersion{};
	PFun_slUpgradeInterface* slUpgradeInterface{};
	PFun_slSetConstants* slSetConstants{};
	PFun_slGetNativeInterface* slGetNativeInterface{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};

	// DLSS specific functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	SL_FUN_DECL(slDLSSDSetOptions);

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	bool isRTXBelow40series = false;

	// Helper: Execute DLSS for a single viewport with given resources
	void EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
		ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth);

	void EvaluateDLSS(ID3D12GraphicsCommandList4* commandList, sl::ViewportHandle vp,
		ID3D12Resource* colorIn, ID3D12Resource* colorOut, ID3D12Resource* depth, ID3D12Resource* mvec, ID3D12Resource* reactiveMask,
		ID3D12Resource* diffuseAlbedo, ID3D12Resource* specularAlbedo, ID3D12Resource* normalRoughness, ID3D12Resource* specHitDistance,
		const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth);

	void EvaluateDLSSD(ID3D12GraphicsCommandList4* commandList, sl::ViewportHandle vp,
		ID3D12Resource* colorIn, ID3D12Resource* colorOut, ID3D12Resource* depth, ID3D12Resource* mvec, ID3D12Resource* reactiveMask,
		ID3D12Resource* diffuseAlbedo, ID3D12Resource* specularAlbedo, ID3D12Resource* normalRoughness, ID3D12Resource* specHitDistance,
		const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth);

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadInterposer(bool a_d3d12Mode);

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();

	void CheckFrameConstants(sl::ViewportHandle p_viewport, uint32_t eyeIndex = 0);

	bool IsRTXAndBelow40Series(IDXGIAdapter* a_adapter);

	void SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width);

	void SetDLSSDOptions(sl::ViewportHandle p_viewport, uint32_t width);

	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);

	void Upscale(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* a_input, ID3D12Resource* a_output, ID3D12Resource* a_depth, ID3D12Resource* a_motionVectors, ID3D12Resource* a_reactiveMask);

	void DenoiseUpscale(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* a_upscalingTexture, ID3D12Resource* a_depth, ID3D12Resource* a_motionVectors, ID3D12Resource* a_reactiveMask);

	void DestroyDLSSResources();
};

DEFINE_ENUM_FLAG_OPERATORS(Streamline::Features);