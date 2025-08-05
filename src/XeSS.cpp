#include "XeSS.h"
#include "Utils/FileSystem.h"

#include "State.h"
#include "Upscaling.h"

// XeSS enums and structures (simplified - these would come from the XeSS SDK headers)
enum xess_result_t
{
	XESS_RESULT_SUCCESS = 0,
	XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY = -1,
	XESS_RESULT_ERROR_DEVICE = -2,
	XESS_RESULT_ERROR_IMPLEMENTATION = -3,
	XESS_RESULT_ERROR_INVALID_ARGUMENT = -4,
	XESS_RESULT_ERROR_NOT_SUPPORTED = -5,
	XESS_RESULT_ERROR_UNINITIALIZED = -6,
	XESS_RESULT_ERROR_INVALID_CONTEXT = -7
};

enum xess_quality_settings_t
{
	XESS_QUALITY_SETTING_PERFORMANCE = 0,
	XESS_QUALITY_SETTING_BALANCED = 1,
	XESS_QUALITY_SETTING_QUALITY = 2,
	XESS_QUALITY_SETTING_ULTRA_QUALITY = 3
};

enum xess_init_flag_bits_t
{
	XESS_INIT_FLAG_NONE = 0,
	XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE = (1 << 0),
	XESS_INIT_FLAG_HIGH_RES_MV = (1 << 1),
	XESS_INIT_FLAG_JITTERED_MV = (1 << 2),
	XESS_INIT_FLAG_INVERTED_DEPTH = (1 << 3),
	XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK = (1 << 4),
	XESS_INIT_FLAG_LDR_INPUT_COLOR = (1 << 5),
	XESS_INIT_FLAG_USE_TAA_HISTORY = (1 << 6)
};

struct xess_d3d11_init_params
{
	uint32_t outputWidth;
	uint32_t outputHeight;
	xess_quality_settings_t qualitySetting;
	uint32_t initFlags;
};

struct xess_d3d11_execute_params
{
	ID3D11Resource* pColorTexture;
	ID3D11Resource* pVelocityTexture;
	ID3D11Resource* pDepthTexture;
	ID3D11Resource* pResponsivePixelMaskTexture;
	ID3D11Resource* pOutputTexture;
	float jitterOffsetX;
	float jitterOffsetY;
	uint32_t inputWidth;
	uint32_t inputHeight;
	float exposureScale;
	float resetHistory;
};

// Define the static member
std::vector<std::pair<std::string, std::string>> XeSS::dllVersions = {};

void XeSS::LoadXeSS()
{
	std::wstring dllPath = std::wstring(XeSS::PluginDir) + L"\\libxess.dll";
	module = LoadLibrary(dllPath.c_str());

	// Cache all DLL versions in the XeSS directory
	std::filesystem::path pluginDir = std::filesystem::path(XeSS::PluginDir);
	XeSS::dllVersions = Util::EnumerateDllVersions(pluginDir);

	if (module) {
		xessGetVersion = (xessGetVersionPtr)GetProcAddress(module, "xessGetVersion");
		xessD3D11CreateContext = (xessD3D11CreateContextPtr)GetProcAddress(module, "xessD3D11CreateContext");
		xessD3D11Init = (xessD3D11InitPtr)GetProcAddress(module, "xessD3D11Init");
		xessD3D11Execute = (xessD3D11ExecutePtr)GetProcAddress(module, "xessD3D11Execute");
		xessDestroyContext = (xessDestroyContextPtr)GetProcAddress(module, "xessDestroyContext");

		if (xessGetVersion && xessD3D11CreateContext && xessD3D11Init && xessD3D11Execute && xessDestroyContext) {
			featureXeSS = true;
			logger::info("[XeSS] Successfully loaded XeSS SDK version: {}", xessGetVersion());
		} else {
			featureXeSS = false;
			logger::error("[XeSS] Failed to load XeSS function pointers");
		}
	} else {
		featureXeSS = false;
		logger::error("[XeSS] Failed to load libxess.dll");
	}
}

void XeSS::CreateXeSSResources()
{
	if (!featureXeSS) {
		logger::error("[XeSS] XeSS not available, cannot create resources");
		return;
	}

	auto state = globals::state;

	if (xessD3D11CreateContext(globals::d3d::device, &xessContext) != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to create XeSS context!");
		return;
	}

	xess_d3d11_init_params initParams{};
	initParams.outputWidth = (uint32_t)state->screenSize.x;
	initParams.outputHeight = (uint32_t)state->screenSize.y;
	initParams.qualitySetting = XESS_QUALITY_SETTING_QUALITY;
	initParams.initFlags = XESS_INIT_FLAG_HIGH_RES_MV | XESS_INIT_FLAG_JITTERED_MV;

	if (xessD3D11Init(xessContext, &initParams) != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to initialize XeSS context!");
		return;
	}

	logger::info("[XeSS] XeSS resources created successfully");
}

void XeSS::DestroyXeSSResources()
{
	if (xessContext && xessDestroyContext) {
		xessDestroyContext(xessContext);
		xessContext = nullptr;
	}
}

void XeSS::Upscale(ID3D11Resource* a_inputTexture, ID3D11Resource* a_outputTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_motionVectors, ID3D11Resource* a_depth, float2 a_jitter)
{
	if (!featureXeSS || !xessContext) {
		logger::error("[XeSS] XeSS not initialized, cannot upscale");
		return;
	}

	auto state = globals::state;
	auto renderSize = Util::ConvertToDynamic(state->screenSize);

	xess_d3d11_execute_params execParams{};
	execParams.pColorTexture = a_inputTexture;
	execParams.pVelocityTexture = a_motionVectors;
	execParams.pDepthTexture = a_depth;
	execParams.pResponsivePixelMaskTexture = a_reactiveMask;
	execParams.pOutputTexture = a_outputTexture;
	execParams.jitterOffsetX = -a_jitter.x;
	execParams.jitterOffsetY = -a_jitter.y;
	execParams.inputWidth = (uint32_t)renderSize.x;
	execParams.inputHeight = (uint32_t)renderSize.y;
	execParams.exposureScale = 1.0f;
	execParams.resetHistory = 0.0f;

	int result = xessD3D11Execute(xessContext, &execParams);
	if (result != XESS_RESULT_SUCCESS) {
		logger::error("[XeSS] Failed to execute XeSS upscaling, error code: {}", result);
	}
}