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
#include <sl_nis.h>
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

	bool featureDLSS = false;
	bool featureDLSS_RR = false;
	bool featureNIS = false;

	sl::ViewportHandle viewport{ 0 };

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

	// DLSSD specific functions
	PFun_slDLSSDGetOptimalSettings* slDLSSDGetOptimalSettings{};
	PFun_slDLSSDGetState* slDLSSDGetState{};
	PFun_slDLSSDSetOptions* slDLSSDSetOptions{};

	// NIS specific functions
	PFun_slNISSetOptions* slNISSetOptions{};
	PFun_slNISGetState* slNISGetState{};

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadInterposer();

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();

	void CheckFrameConstants();

	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);

	void RayReconstruction(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_outputTexture, ID3D11Resource* a_normalRoughness, ID3D11Resource* a_specularHitDistance, ID3D11Resource* a_motionVectors);

	float GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityPreset);
	float GetInputResolutionScaleRR(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityPreset);

	void DestroyDLSSResources(bool keepOptions = false);
	void DestroyDLSSRRResources(bool keepOptions = false);

	void ApplyNISSharpening(ID3D11Resource* a_texture, float sharpness);
};
