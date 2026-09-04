#include "IntelXeSSFrameGeneration.h"

#include "../../State.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace
{
	constexpr wchar_t kXeFGDllName[] = L"libxess_fg.dll";
	constexpr wchar_t kXeLLDllName[] = L"libxell.dll";

	std::filesystem::path GetExecutableDirectory()
	{
		std::array<wchar_t, 32768> executablePath{};
		const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (length == 0 || length >= executablePath.size())
			return {};

		return std::filesystem::path(executablePath.data(), executablePath.data() + length).parent_path();
	}

	template <class T>
	bool ResolveExport(HMODULE module, const char* name, T& destination)
	{
		static_assert(sizeof(T) == sizeof(FARPROC));
		const FARPROC address = GetProcAddress(module, name);
		if (!address) {
			logger::error("[XeSS-FG] Required SDK export '{}' is missing", name);
			destination = nullptr;
			return false;
		}

		std::memcpy(&destination, &address, sizeof(destination));
		return true;
	}

	/** Resolves an export that older SDK builds may not provide. Never fails the load. */
	template <class T>
	bool ResolveOptionalExport(HMODULE module, const char* name, T& destination)
	{
		static_assert(sizeof(T) == sizeof(FARPROC));
		const FARPROC address = GetProcAddress(module, name);
		if (!address) {
			logger::info("[XeSS-FG] Optional SDK export '{}' is missing; the matching feature stays disabled", name);
			destination = nullptr;
			return false;
		}

		std::memcpy(&destination, &address, sizeof(destination));
		return true;
	}

	const char* XeFGResultName(xefg_swapchain_result_t result)
	{
		switch (result) {
		case XEFG_SWAPCHAIN_RESULT_SUCCESS:
			return "SUCCESS";
		case XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER:
			return "WARNING_OLD_DRIVER";
		case XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES:
			return "WARNING_TOO_FEW_FRAMES";
		case XEFG_SWAPCHAIN_RESULT_WARNING_FRAMES_ID_MISMATCH:
			return "WARNING_FRAMES_ID_MISMATCH";
		case XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS:
			return "WARNING_MISSING_PRESENT_STATUS";
		case XEFG_SWAPCHAIN_RESULT_WARNING_RESOURCE_SIZES_MISMATCH:
			return "WARNING_RESOURCE_SIZES_MISMATCH";
		case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE:
			return "ERROR_UNSUPPORTED_DEVICE";
		case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DRIVER:
			return "ERROR_UNSUPPORTED_DRIVER";
		case XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED:
			return "ERROR_UNINITIALIZED";
		case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT:
			return "ERROR_INVALID_ARGUMENT";
		case XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:
			return "ERROR_DEVICE_OUT_OF_MEMORY";
		case XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE:
			return "ERROR_DEVICE";
		case XEFG_SWAPCHAIN_RESULT_ERROR_NOT_IMPLEMENTED:
			return "ERROR_NOT_IMPLEMENTED";
		case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_CONTEXT:
			return "ERROR_INVALID_CONTEXT";
		case XEFG_SWAPCHAIN_RESULT_ERROR_OPERATION_IN_PROGRESS:
			return "ERROR_OPERATION_IN_PROGRESS";
		case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED:
			return "ERROR_UNSUPPORTED";
		case XEFG_SWAPCHAIN_RESULT_ERROR_CANT_LOAD_LIBRARY:
			return "ERROR_CANT_LOAD_LIBRARY";
		case XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES:
			return "ERROR_MISMATCH_INPUT_RESOURCES";
		case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_OUTPUT_RESOURCES:
			return "ERROR_INCORRECT_OUTPUT_RESOURCES";
		case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_INPUT_RESOURCES:
			return "ERROR_INCORRECT_INPUT_RESOURCES";
		case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_UNSUPPORTED:
			return "ERROR_LATENCY_REDUCTION_UNSUPPORTED";
		case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_FUNCTION_MISSING:
			return "ERROR_LATENCY_REDUCTION_FUNCTION_MISSING";
		case XEFG_SWAPCHAIN_RESULT_ERROR_HRESULT_FAILURE:
			return "ERROR_HRESULT_FAILURE";
		case XEFG_SWAPCHAIN_RESULT_ERROR_DXGI_INVALID_CALL:
			return "ERROR_DXGI_INVALID_CALL";
		case XEFG_SWAPCHAIN_RESULT_ERROR_POINTER_STILL_IN_USE:
			return "ERROR_POINTER_STILL_IN_USE";
		case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_DESCRIPTOR_HEAP:
			return "ERROR_INVALID_DESCRIPTOR_HEAP";
		case XEFG_SWAPCHAIN_RESULT_ERROR_WRONG_CALL_ORDER:
			return "ERROR_WRONG_CALL_ORDER";
		case XEFG_SWAPCHAIN_RESULT_ERROR_UNKNOWN:
			return "ERROR_UNKNOWN";
		default:
			return "UNKNOWN_RESULT";
		}
	}

	const char* XeFGUiModeName(xefg_swapchain_ui_mode_t mode)
	{
		switch (mode) {
		case XEFG_SWAPCHAIN_UI_MODE_AUTO:
			return "AUTO";
		case XEFG_SWAPCHAIN_UI_MODE_NONE:
			return "NONE (interpolate UI)";
		case XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_UITEXTURE:
			return "BACKBUFFER_UITEXTURE";
		case XEFG_SWAPCHAIN_UI_MODE_HUDLESS_UITEXTURE:
			return "HUDLESS_UITEXTURE";
		case XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS:
			return "BACKBUFFER_HUDLESS";
		case XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS_UITEXTURE:
			return "BACKBUFFER_HUDLESS_UITEXTURE";
		default:
			return "UNKNOWN";
		}
	}

	const char* XeLLResultName(xell_result_t result)
	{
		switch (result) {
		case XELL_RESULT_SUCCESS:
			return "SUCCESS";
		case XELL_RESULT_ERROR_UNSUPPORTED_DEVICE:
			return "ERROR_UNSUPPORTED_DEVICE";
		case XELL_RESULT_ERROR_UNSUPPORTED_DRIVER:
			return "ERROR_UNSUPPORTED_DRIVER";
		case XELL_RESULT_ERROR_UNINITIALIZED:
			return "ERROR_UNINITIALIZED";
		case XELL_RESULT_ERROR_INVALID_ARGUMENT:
			return "ERROR_INVALID_ARGUMENT";
		case XELL_RESULT_ERROR_DEVICE:
			return "ERROR_DEVICE";
		case XELL_RESULT_ERROR_NOT_IMPLEMENTED:
			return "ERROR_NOT_IMPLEMENTED";
		case XELL_RESULT_ERROR_INVALID_CONTEXT:
			return "ERROR_INVALID_CONTEXT";
		case XELL_RESULT_ERROR_UNSUPPORTED:
			return "ERROR_UNSUPPORTED";
		case XELL_RESULT_ERROR_UNKNOWN:
			return "ERROR_UNKNOWN";
		default:
			return "UNKNOWN_RESULT";
		}
	}

	bool CheckXeFGResult(xefg_swapchain_result_t result, std::string_view operation)
	{
		const int32_t value = static_cast<int32_t>(result);
		if (value < 0) {
			logger::error("[XeSS-FG] {} failed: {} ({})", operation, XeFGResultName(result), value);
			return false;
		}
		if (value > 0)
			logger::warn("[XeSS-FG] {} returned {} ({})", operation, XeFGResultName(result), value);
		return true;
	}

	bool CheckXeLLResult(xell_result_t result, std::string_view operation)
	{
		if (result == XELL_RESULT_SUCCESS)
			return true;

		logger::error("[XeLL] {} failed: {} ({})", operation, XeLLResultName(result), static_cast<int32_t>(result));
		return false;
	}

	bool IsFlipModel(DXGI_SWAP_EFFECT effect)
	{
		return effect == DXGI_SWAP_EFFECT_FLIP_DISCARD || effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	}

	bool ResourceContainsRegion(ID3D12Resource* resource, uint32_t width, uint32_t height)
	{
		if (!resource || width == 0 || height == 0)
			return false;

		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Width >= width && desc.Height >= height;
	}

	bool ResourceExactlyMatches(ID3D12Resource* resource, uint32_t width, uint32_t height, DXGI_FORMAT format)
	{
		if (!resource)
			return false;

		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		       desc.Width == width && desc.Height == height && desc.Format == format;
	}

	uint64_t GetGlobalsFrame(uint64_t fallback)
	{
		if (globals::state)
			return globals::state->frameCountAtomic.load(std::memory_order_relaxed);
		return fallback;
	}
}

IntelXeSSFrameGeneration::~IntelXeSSFrameGeneration()
{
	Shutdown();
}

void IntelXeSSFrameGeneration::XeFGLogCallback(
	const char* message, xefg_swapchain_logging_level_t level, [[maybe_unused]] void* userData)
{
	try {
		const char* text = message ? message : "<null message>";
		switch (level) {
		case XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG:
			logger::debug("[XeSS-FG SDK] {}", text);
			break;
		case XEFG_SWAPCHAIN_LOGGING_LEVEL_INFO:
			logger::info("[XeSS-FG SDK] {}", text);
			break;
		case XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING:
			logger::warn("[XeSS-FG SDK] {}", text);
			break;
		case XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR:
		default:
			logger::error("[XeSS-FG SDK] {}", text);
			break;
		}
	} catch (...) {
	}
}

void IntelXeSSFrameGeneration::XeLLLogCallback(const char* message, xell_logging_level_t level)
{
	try {
		const char* text = message ? message : "<null message>";
		switch (level) {
		case XELL_LOGGING_LEVEL_DEBUG:
			logger::debug("[XeLL SDK] {}", text);
			break;
		case XELL_LOGGING_LEVEL_INFO:
			logger::info("[XeLL SDK] {}", text);
			break;
		case XELL_LOGGING_LEVEL_WARNING:
			logger::warn("[XeLL SDK] {}", text);
			break;
		case XELL_LOGGING_LEVEL_ERROR:
		default:
			logger::error("[XeLL SDK] {}", text);
			break;
		}
	} catch (...) {
	}
}

bool IntelXeSSFrameGeneration::ResolveExportsUnlocked()
{
	bool resolved = true;

	resolved &= ResolveExport(xefgModule_, "xefgSwapChainGetVersion", xefg_.getVersion);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainGetProperties", xefg_.getProperties);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainTagFrameConstants", xefg_.tagFrameConstants);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainSetEnabled", xefg_.setEnabled);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainSetPresentId", xefg_.setPresentId);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainGetLastPresentStatus", xefg_.getLastPresentStatus);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainSetLoggingCallback", xefg_.setLoggingCallback);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainDestroy", xefg_.destroy);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainSetLatencyReduction", xefg_.setLatencyReduction);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainSetUiCompositionState", xefg_.setUiCompositionState);
	ResolveOptionalExport(xefgModule_, "xefgSwapChainSetNumInterpolatedFrames", xefg_.setNumInterpolatedFrames);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainD3D12CreateContext", xefg_.d3d12CreateContext);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainD3D12GetProperties", xefg_.d3d12GetProperties);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainD3D12InitFromSwapChainDesc", xefg_.d3d12InitFromSwapChainDesc);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainD3D12GetSwapChainPtr", xefg_.d3d12GetSwapChainPtr);
	resolved &= ResolveExport(xefgModule_, "xefgSwapChainD3D12TagFrameResource", xefg_.d3d12TagFrameResource);

	resolved &= ResolveExport(xellModule_, "xellGetVersion", xell_.getVersion);
	resolved &= ResolveExport(xellModule_, "xellD3D12CreateContext", xell_.d3d12CreateContext);
	resolved &= ResolveExport(xellModule_, "xellDestroyContext", xell_.destroy);
	resolved &= ResolveExport(xellModule_, "xellSetSleepMode", xell_.setSleepMode);
	resolved &= ResolveExport(xellModule_, "xellSleep", xell_.sleep);
	resolved &= ResolveExport(xellModule_, "xellAddMarkerData", xell_.addMarkerData);
	resolved &= ResolveExport(xellModule_, "xellSetLoggingCallback", xell_.setLoggingCallback);

	return resolved;
}

void IntelXeSSFrameGeneration::UnloadModulesUnlocked()
{
	apiAvailable_ = false;
	xefg_ = {};
	xell_ = {};

	if (xefgModule_) {
		FreeLibrary(xefgModule_);
		xefgModule_ = nullptr;
	}
	if (xellModule_) {
		FreeLibrary(xellModule_);
		xellModule_ = nullptr;
	}
}

bool IntelXeSSFrameGeneration::Load()
{
	std::scoped_lock lock(mutex_);
	if (apiAvailable_)
		return true;
	if (xefgContext_ || xellContext_) {
		logger::error("[XeSS-FG] Cannot reload SDK DLLs while SDK contexts still exist");
		return false;
	}

	UnloadModulesUnlocked();

	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	if (executableDirectory.empty()) {
		logger::error("[XeSS-FG] Failed to resolve the game executable directory (Win32 error {})", GetLastError());
		return false;
	}

	const std::filesystem::path sdkDirectory = executableDirectory / PluginDir;
	const std::filesystem::path xellPath = sdkDirectory / kXeLLDllName;
	const std::filesystem::path xefgPath = sdkDirectory / kXeFGDllName;
	constexpr DWORD loadFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;

	xellModule_ = LoadLibraryExW(xellPath.c_str(), nullptr, loadFlags);
	if (!xellModule_) {
		logger::warn("[XeSS-FG] XeLL is unavailable: failed to load '{}' (Win32 error {})", xellPath.string(), GetLastError());
		UnloadModulesUnlocked();
		return false;
	}

	xefgModule_ = LoadLibraryExW(xefgPath.c_str(), nullptr, loadFlags);
	if (!xefgModule_) {
		logger::warn("[XeSS-FG] Frame generation is unavailable: failed to load '{}' (Win32 error {})", xefgPath.string(), GetLastError());
		UnloadModulesUnlocked();
		return false;
	}

	if (!ResolveExportsUnlocked()) {
		logger::error("[XeSS-FG] SDK DLLs do not expose the complete XeSS-FG 1.3/XeLL 1.3 API");
		UnloadModulesUnlocked();
		return false;
	}

	xefg_swapchain_version_t xefgVersion{};
	if (CheckXeFGResult(xefg_.getVersion(&xefgVersion), "query SDK version"))
		logger::info("[XeSS-FG] Loaded SDK {}.{}.{}", xefgVersion.major, xefgVersion.minor, xefgVersion.patch);

	xell_version_t xellVersion{};
	if (CheckXeLLResult(xell_.getVersion(&xellVersion), "query SDK version"))
		logger::info("[XeLL] Loaded SDK {}.{}.{}", xellVersion.major, xellVersion.minor, xellVersion.patch);

	apiAvailable_ = true;
	return true;
}

bool IntelXeSSFrameGeneration::IsAvailable() const
{
	std::scoped_lock lock(mutex_);
	return apiAvailable_;
}

bool IntelXeSSFrameGeneration::IsInitialized() const
{
	std::scoped_lock lock(mutex_);
	return initialized_;
}

bool IntelXeSSFrameGeneration::CreateContextAndSwapChain(
	ID3D12Device* device,
	ID3D12CommandQueue* commandQueue,
	IDXGIFactory2* factory,
	HWND window,
	const DXGI_SWAP_CHAIN_DESC1& swapChainDesc,
	DXGI_FORMAT hudlessFormat,
	DXGI_FORMAT uiFormat)
{
	CreateInfo info{};
	info.device = device;
	info.commandQueue = commandQueue;
	info.factory = factory;
	info.window = window;
	info.swapChainDesc = swapChainDesc;
	info.hudlessFormat = hudlessFormat;
	info.uiFormat = uiFormat;
	return CreateContextAndSwapChain(info);
}

bool IntelXeSSFrameGeneration::CreateContextAndSwapChain(const CreateInfo& info)
{
	if (!Load())
		return false;

	std::scoped_lock lock(mutex_);
	if (!apiAvailable_)
		return false;

	if (!info.device || !info.commandQueue || !info.factory || !info.window) {
		logger::error("[XeSS-FG] Cannot create contexts: device, direct queue, factory, and HWND are required");
		return false;
	}
	if (info.commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		logger::error("[XeSS-FG] Cannot create proxy swap chain with a non-direct D3D12 command queue");
		return false;
	}
	if (info.swapChainDesc.Width == 0 || info.swapChainDesc.Height == 0 ||
		info.swapChainDesc.Format == DXGI_FORMAT_UNKNOWN || info.swapChainDesc.BufferCount < 2) {
		logger::error("[XeSS-FG] Invalid swap-chain description ({}x{}, format {}, buffers {})",
			info.swapChainDesc.Width,
			info.swapChainDesc.Height,
			static_cast<uint32_t>(info.swapChainDesc.Format),
			info.swapChainDesc.BufferCount);
		return false;
	}
	if (!IsFlipModel(info.swapChainDesc.SwapEffect)) {
		logger::error("[XeSS-FG] XeLL requires a DXGI flip-model swap chain");
		return false;
	}
	if (info.maxInterpolatedFrames == 0) {
		logger::error("[XeSS-FG] maxInterpolatedFrames must be at least one");
		return false;
	}

	if (initialized_ || xefgContext_ || xellContext_) {
		logger::warn("[XeSS-FG] Replacing an existing backend context");
		if (!DestroyContextsUnlocked(false))
			return false;
	}

	device_.copy_from(info.device);
	commandQueue_.copy_from(info.commandQueue);
	swapChainFormat_ = info.swapChainDesc.Format;
	displayWidth_ = info.swapChainDesc.Width;
	displayHeight_ = info.swapChainDesc.Height;

	if (!CheckXeLLResult(xell_.d3d12CreateContext(info.device, &xellContext_), "create D3D12 context")) {
		DestroyContextsUnlocked(false);
		return false;
	}
	if (!CheckXeLLResult(
			xell_.setLoggingCallback(xellContext_, XELL_LOGGING_LEVEL_WARNING, XeLLLogCallback),
			"register logging callback")) {
		logger::warn("[XeLL] Continuing without the SDK logging callback");
	}

	if (!CheckXeFGResult(xefg_.d3d12CreateContext(info.device, &xefgContext_), "create D3D12 context")) {
		DestroyContextsUnlocked(false);
		return false;
	}
	if (!CheckXeFGResult(
			xefg_.setLoggingCallback(xefgContext_, XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING, XeFGLogCallback, this),
			"register logging callback")) {
		logger::warn("[XeSS-FG] Continuing without the SDK logging callback");
	}
	if (!CheckXeFGResult(xefg_.setLatencyReduction(xefgContext_, xellContext_), "connect XeLL context")) {
		DestroyContextsUnlocked(false);
		return false;
	}

	constexpr uint32_t supportedFlags =
		XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH |
		XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV |
		XEFG_SWAPCHAIN_INIT_FLAG_USE_NDC_VELOCITY |
		XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV |
		XEFG_SWAPCHAIN_INIT_FLAG_UITEXTURE_NOT_PREMUL_ALPHA;
	const uint32_t initFlags = info.initFlags & supportedFlags;
	if (initFlags != info.initFlags) {
		logger::warn("[XeSS-FG] Ignoring unsupported or external-allocation initialization flags: 0x{:X}",
			info.initFlags & ~supportedFlags);
	}

	const bool uiFormatsCompatible = info.hudlessFormat != DXGI_FORMAT_UNKNOWN &&
	                                 info.uiFormat != DXGI_FORMAT_UNKNOWN &&
	                                 info.hudlessFormat == info.swapChainDesc.Format &&
	                                 info.uiFormat == info.swapChainDesc.Format;
	uiCompositionSupported_ = info.enableUiCompositionWhenCompatible &&
	                          info.uiMode != XEFG_SWAPCHAIN_UI_MODE_NONE &&
	                          uiFormatsCompatible;
	if (info.enableUiCompositionWhenCompatible && info.uiMode != XEFG_SWAPCHAIN_UI_MODE_NONE && !uiFormatsCompatible) {
		logger::info("[XeSS-FG] UI/HUD-less formats do not match the backbuffer; using final-backbuffer UI interpolation");
	}
	uiMode_ = uiCompositionSupported_ ? info.uiMode : XEFG_SWAPCHAIN_UI_MODE_NONE;

	xefg_swapchain_d3d12_init_params_t initParams{};
	initParams.initFlags = initFlags;
	initParams.maxInterpolatedFrames = info.maxInterpolatedFrames;
	if (!xefg_.setNumInterpolatedFrames && initParams.maxInterpolatedFrames != 1) {
		// Without the runtime setter the swap chain is stuck at whatever it is initialized with,
		// and the SDK defaults to the maximum. Pin it to single-frame generation instead.
		logger::info("[XeSS-FG] SDK has no multi-frame generation support; initializing for one interpolated frame");
		initParams.maxInterpolatedFrames = 1;
	}
	initParams.uiMode = uiMode_;

	xefg_swapchain_properties_t capabilityProperties{};
	if (!CheckXeFGResult(
			xefg_.getProperties(xefgContext_, &capabilityProperties),
			"query interpolation capabilities")) {
		DestroyContextsUnlocked(false);
		return false;
	}
	if (initParams.maxInterpolatedFrames != XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES &&
		capabilityProperties.maxSupportedInterpolations != 0 &&
		initParams.maxInterpolatedFrames > capabilityProperties.maxSupportedInterpolations) {
		logger::warn("[XeSS-FG] Requested {} interpolated frames, clamping to supported maximum {}",
			initParams.maxInterpolatedFrames,
			capabilityProperties.maxSupportedInterpolations);
		initParams.maxInterpolatedFrames = capabilityProperties.maxSupportedInterpolations;
	}

	properties_ = {};
	if (!CheckXeFGResult(
			xefg_.d3d12GetProperties(
				xefgContext_,
				&initParams,
				info.swapChainDesc.Width,
				info.swapChainDesc.Height,
				info.swapChainDesc.Format,
				&properties_),
			"query initialization properties")) {
		DestroyContextsUnlocked(false);
		return false;
	}

	if (info.fullscreenDesc && !info.fullscreenDesc->Windowed)
		logger::warn("[XeSS-FG] Exclusive fullscreen can initialize, but frame generation cannot be enabled there");

	if (!CheckXeFGResult(
			xefg_.d3d12InitFromSwapChainDesc(
				xefgContext_,
				info.window,
				&info.swapChainDesc,
				info.fullscreenDesc,
				info.commandQueue,
				info.factory,
				&initParams),
			"initialize proxy swap chain from description")) {
		DestroyContextsUnlocked(false);
		return false;
	}

	IDXGISwapChain4* proxySwapChain = nullptr;
	if (!CheckXeFGResult(
			xefg_.d3d12GetSwapChainPtr(xefgContext_, __uuidof(IDXGISwapChain4), reinterpret_cast<void**>(&proxySwapChain)),
			"get IDXGISwapChain4 proxy") ||
		!proxySwapChain) {
		DestroyContextsUnlocked(false);
		return false;
	}
	swapChain_.attach(proxySwapChain);
	initialized_ = true;

	// The SDK starts out producing the maximum number of interpolated frames the swap chain was
	// initialized with, so the caller must set the multiplier it actually wants before presenting.
	maxInterpolatedFrames_ = capabilityProperties.maxSupportedInterpolations;
	if (initParams.maxInterpolatedFrames != XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES) {
		maxInterpolatedFrames_ = maxInterpolatedFrames_ ?
		                             std::min(maxInterpolatedFrames_, initParams.maxInterpolatedFrames) :
		                             initParams.maxInterpolatedFrames;
	}
	if (maxInterpolatedFrames_ == 0)
		maxInterpolatedFrames_ = 1;
	numInterpolatedFrames_ = maxInterpolatedFrames_;

	if (!UpdateUiCompositionUnlocked(uiCompositionSupported_)) {
		DestroyContextsUnlocked(false);
		return false;
	}

	forceHistoryReset_ = true;
	logger::info("[XeSS-FG] Proxy swap chain initialized ({}x{}, format {}, max interpolations {}, UI mode {}, UI composition {})",
		displayWidth_,
		displayHeight_,
		static_cast<uint32_t>(swapChainFormat_),
		maxInterpolatedFrames_,
		XeFGUiModeName(uiMode_),
		uiCompositionEnabled_ ? "enabled" : "disabled");
	return true;
}

bool IntelXeSSFrameGeneration::SetNumInterpolatedFrames(uint32_t interpolatedFrames)
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xefgContext_)
		return false;
	if (!xefg_.setNumInterpolatedFrames)
		return false;

	const uint32_t clamped = std::clamp(interpolatedFrames, 1U, std::max(1U, maxInterpolatedFrames_));
	if (clamped == numInterpolatedFrames_)
		return true;

	// Reallocates internal resources, so only ever called when the value actually changes.
	if (!CheckXeFGResult(xefg_.setNumInterpolatedFrames(xefgContext_, clamped), "set interpolated frame count"))
		return false;

	numInterpolatedFrames_ = clamped;
	forceHistoryReset_ = true;
	logger::info("[XeSS-FG] Now generating {} interpolated frame(s) per rendered frame", clamped);
	return true;
}

uint32_t IntelXeSSFrameGeneration::GetMaxInterpolatedFrames() const
{
	std::scoped_lock lock(mutex_);
	return maxInterpolatedFrames_;
}

uint32_t IntelXeSSFrameGeneration::GetNumInterpolatedFrames() const
{
	std::scoped_lock lock(mutex_);
	return initialized_ ? numInterpolatedFrames_ : 0;
}

bool IntelXeSSFrameGeneration::SupportsMultiFrameGeneration() const
{
	std::scoped_lock lock(mutex_);
	return xefg_.setNumInterpolatedFrames != nullptr && maxInterpolatedFrames_ > 1;
}

uint32_t IntelXeSSFrameGeneration::GetUiMode() const
{
	std::scoped_lock lock(mutex_);
	return static_cast<uint32_t>(uiMode_);
}

IDXGISwapChain4* IntelXeSSFrameGeneration::GetSwapChain() const
{
	std::scoped_lock lock(mutex_);
	return swapChain_.get();
}

bool IntelXeSSFrameGeneration::SetEnabled(bool enabled, uint32_t minimumIntervalUs)
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xefgContext_ || !xellContext_)
		return false;

	if (enabled) {
		if (!xellLowLatencyEnabled_ || minimumIntervalUs_ != minimumIntervalUs) {
			xell_sleep_params_t sleepParams{};
			sleepParams.bLowLatencyMode = 1;
			sleepParams.minimumIntervalUs = minimumIntervalUs;
			if (!CheckXeLLResult(xell_.setSleepMode(xellContext_, &sleepParams), "enable latency reduction"))
				return false;
			xellLowLatencyEnabled_ = true;
			minimumIntervalUs_ = minimumIntervalUs;
			// The interval caps the rendered frame rate; presented is that times the multiplier.
			if (minimumIntervalUs)
				logger::info("[XeLL] Low latency mode enabled, minimum frame interval {} us ({:.1f} fps render cap)", minimumIntervalUs, 1000000.0 / minimumIntervalUs);
			else
				logger::info("[XeLL] Low latency mode enabled, no frame interval cap");
		}

		if (!enabled_) {
			if (!CheckXeFGResult(xefg_.setEnabled(xefgContext_, 1), "enable frame generation")) {
				xell_sleep_params_t disabledParams{};
				if (CheckXeLLResult(xell_.setSleepMode(xellContext_, &disabledParams), "roll back latency reduction")) {
					xellLowLatencyEnabled_ = false;
					minimumIntervalUs_ = 0;
				}
				return false;
			}
			enabled_ = true;
			forceHistoryReset_ = true;
		}
		return true;
	}

	if (enabled_) {
		if (!CheckXeFGResult(xefg_.setEnabled(xefgContext_, 0), "disable frame generation"))
			return false;
		enabled_ = false;
		active_ = false;
	}

	if (xellLowLatencyEnabled_) {
		xell_sleep_params_t disabledParams{};
		if (!CheckXeLLResult(xell_.setSleepMode(xellContext_, &disabledParams), "disable latency reduction"))
			return false;
		xellLowLatencyEnabled_ = false;
		minimumIntervalUs_ = 0;
	}
	return true;
}

bool IntelXeSSFrameGeneration::SleepModeWouldChange(bool enabled, uint32_t minimumIntervalUs) const
{
	std::scoped_lock lock(mutex_);
	if (!initialized_)
		return false;
	if (enabled)
		return !xellLowLatencyEnabled_ || minimumIntervalUs_ != minimumIntervalUs;
	return xellLowLatencyEnabled_ || enabled_;
}

bool IntelXeSSFrameGeneration::BeginRenderSubmit()
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xellContext_ || frameStage_ != FrameStage::Simulation)
		return false;

	bool success = AddMarkerUnlocked(XELL_SIMULATION_END);
	success &= AddMarkerUnlocked(XELL_RENDERSUBMIT_START);
	frameStage_ = FrameStage::RenderSubmit;
	return success;
}

bool IntelXeSSFrameGeneration::BeginFrame()
{
	uint64_t fallback = 0;
	{
		std::scoped_lock lock(mutex_);
		fallback = fallbackGlobalsFrame_;
	}
	return BeginFrame(GetGlobalsFrame(fallback));
}

bool IntelXeSSFrameGeneration::BeginFrame(uint64_t globalsFrame)
{
	std::scoped_lock lock(mutex_);
	return StartFrameUnlocked(globalsFrame);
}

bool IntelXeSSFrameGeneration::StartFrameUnlocked(uint64_t globalsFrame)
{
	if (!initialized_ || !xellContext_)
		return false;

	if (globalsFrame == lastGlobalsFrame_)
		return true;

	bool success = true;
	if (frameStage_ != FrameStage::Idle) {
		if (!warnedIncompleteFrame_) {
			warnedIncompleteFrame_ = true;
			logger::warn("[XeSS-FG] A new game frame started before the previous XeLL marker sequence completed; closing it safely");
		}
		success &= CompleteOpenFrameUnlocked();
	}

	lastGlobalsFrame_ = globalsFrame;
	currentPresentId_ = nextPresentId_++;
	if (nextPresentId_ == 0)
		nextPresentId_ = 1;
	taggedThisFrame_ = false;
	inputSampleSent_ = false;

	if (!CheckXeLLResult(xell_.sleep(xellContext_, currentPresentId_), "sleep at frame start"))
		success = false;
	frameStage_ = FrameStage::Simulation;
	success &= AddMarkerUnlocked(XELL_SIMULATION_START);
	success &= AddMarkerUnlocked(XELL_INPUT_SAMPLE);
	inputSampleSent_ = true;
	return success;
}

bool IntelXeSSFrameGeneration::AddMarkerUnlocked(xell_latency_marker_type_t marker)
{
	if (!xellContext_)
		return false;
	return CheckXeLLResult(xell_.addMarkerData(xellContext_, currentPresentId_, marker), "submit frame marker");
}

bool IntelXeSSFrameGeneration::CompleteOpenFrameUnlocked()
{
	bool success = true;
	if (frameStage_ == FrameStage::Simulation) {
		success &= AddMarkerUnlocked(XELL_SIMULATION_END);
		success &= AddMarkerUnlocked(XELL_RENDERSUBMIT_START);
		frameStage_ = FrameStage::RenderSubmit;
	}
	if (frameStage_ == FrameStage::RenderSubmit) {
		success &= AddMarkerUnlocked(XELL_RENDERSUBMIT_END);
		success &= AddMarkerUnlocked(XELL_PRESENT_START);
		frameStage_ = FrameStage::Present;
	}
	if (frameStage_ == FrameStage::Present)
		success &= AddMarkerUnlocked(XELL_PRESENT_END);

	frameStage_ = FrameStage::Idle;
	taggedThisFrame_ = false;
	active_ = false;
	return success;
}

bool IntelXeSSFrameGeneration::UpdateUiCompositionUnlocked(bool enabled)
{
	const bool desired = enabled && uiCompositionSupported_;
	if (desired == uiCompositionEnabled_)
		return true;
	if (!initialized_ || !xefgContext_)
		return false;

	const auto state = desired ?
	                       XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED :
	                       XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED;
	if (!CheckXeFGResult(xefg_.setUiCompositionState(xefgContext_, state),
			desired ? "enable UI composition" : "disable UI composition")) {
		return false;
	}

	uiCompositionEnabled_ = desired;
	// Logged so a runtime flip (resize, incompatible inputs) is visible in the log rather than
	// only inferable from the presented image.
	logger::info("[XeSS-FG] UI composition {} ({})", desired ? "enabled" : "disabled", XeFGUiModeName(uiMode_));
	return true;
}

bool IntelXeSSFrameGeneration::TagResourceUnlocked(
	ID3D12CommandList* commandList,
	xefg_swapchain_resource_type_t type,
	xefg_swapchain_resource_validity_t validity,
	ID3D12Resource* resource,
	uint32_t width,
	uint32_t height,
	D3D12_RESOURCE_STATES incomingState)
{
	if (!resource || width == 0 || height == 0)
		return false;
	if (validity == XEFG_SWAPCHAIN_RV_ONLY_NOW && !commandList) {
		logger::error("[XeSS-FG] Resource type {} is valid only now but no tagging command list was provided", static_cast<uint32_t>(type));
		return false;
	}

	xefg_swapchain_d3d12_resource_data_t data{};
	data.type = type;
	data.validity = validity;
	data.resourceBase = { 0, 0 };
	data.resourceSize = { width, height };
	data.pResource = resource;
	data.incomingState = incomingState;
	ID3D12CommandList* lifetimeCommandList = validity == XEFG_SWAPCHAIN_RV_ONLY_NOW ? commandList : nullptr;
	return CheckXeFGResult(
		xefg_.d3d12TagFrameResource(xefgContext_, lifetimeCommandList, currentPresentId_, &data),
		"tag frame resource");
}

bool IntelXeSSFrameGeneration::TagFrame(const FrameData& frame)
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xefgContext_ || !xellContext_)
		return false;

	bool success = true;
	if (frameStage_ == FrameStage::Idle) {
		if (!warnedMissingBeginFrame_) {
			warnedMissingBeginFrame_ = true;
			logger::warn("[XeSS-FG] TagFrame ran before BeginFrame; XeLL sleep is occurring later than intended");
		}
		const uint64_t currentGlobalsFrame = GetGlobalsFrame(fallbackGlobalsFrame_);
		if (currentGlobalsFrame == lastGlobalsFrame_ || !StartFrameUnlocked(currentGlobalsFrame))
			return false;
	}
	if (frameStage_ == FrameStage::Simulation) {
		// BeginRenderSubmit normally closed simulation at render start; this is the fallback.
		success &= AddMarkerUnlocked(XELL_SIMULATION_END);
		success &= AddMarkerUnlocked(XELL_RENDERSUBMIT_START);
		frameStage_ = FrameStage::RenderSubmit;
	} else if (frameStage_ != FrameStage::RenderSubmit) {
		logger::error("[XeSS-FG] TagFrame called after the render-submit phase was already closed");
		return false;
	}

	if (!enabled_)
		return success;

	if (!frame.depth || !frame.motionVectors || !frame.viewMatrix || !frame.projectionMatrix ||
		frame.renderWidth == 0 || frame.renderHeight == 0) {
		logger::error("[XeSS-FG] Missing required depth, motion-vector, matrix, or render-size frame input");
		return false;
	}
	if (!ResourceContainsRegion(frame.depth, frame.renderWidth, frame.renderHeight) ||
		!ResourceContainsRegion(frame.motionVectors, frame.renderWidth, frame.renderHeight)) {
		logger::error("[XeSS-FG] Depth and motion-vector resources do not contain the requested {}x{} region",
			frame.renderWidth,
			frame.renderHeight);
		return false;
	}
	if (frame.resourceValidity != XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT &&
		frame.resourceValidity != XEFG_SWAPCHAIN_RV_ONLY_NOW) {
		logger::error("[XeSS-FG] Invalid resource validity value {}", static_cast<uint32_t>(frame.resourceValidity));
		return false;
	}

	const uint32_t displayWidth = frame.displayWidth ? frame.displayWidth : displayWidth_;
	const uint32_t displayHeight = frame.displayHeight ? frame.displayHeight : displayHeight_;
	const bool compatibleUiInputs = uiCompositionSupported_ &&
	                                ResourceExactlyMatches(frame.hudlessColor, displayWidth, displayHeight, swapChainFormat_) &&
	                                ResourceExactlyMatches(frame.uiTexture, displayWidth, displayHeight, swapChainFormat_);
	if (!UpdateUiCompositionUnlocked(compatibleUiInputs))
		return false;

	if (uiCompositionEnabled_) {
		success &= TagResourceUnlocked(
			frame.taggingCommandList,
			XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,
			frame.resourceValidity,
			frame.hudlessColor,
			displayWidth,
			displayHeight,
			frame.hudlessState);
		success &= TagResourceUnlocked(
			frame.taggingCommandList,
			XEFG_SWAPCHAIN_RES_UI,
			frame.resourceValidity,
			frame.uiTexture,
			displayWidth,
			displayHeight,
			frame.uiState);
	}

	success &= TagResourceUnlocked(
		frame.taggingCommandList,
		XEFG_SWAPCHAIN_RES_MOTION_VECTOR,
		frame.resourceValidity,
		frame.motionVectors,
		frame.renderWidth,
		frame.renderHeight,
		frame.motionVectorState);
	success &= TagResourceUnlocked(
		frame.taggingCommandList,
		XEFG_SWAPCHAIN_RES_DEPTH,
		frame.resourceValidity,
		frame.depth,
		frame.renderWidth,
		frame.renderHeight,
		frame.depthState);

	if (!std::isfinite(frame.jitterOffsetX) || !std::isfinite(frame.jitterOffsetY) ||
		!std::isfinite(frame.motionVectorScaleX) || !std::isfinite(frame.motionVectorScaleY)) {
		logger::error("[XeSS-FG] Frame constants contain non-finite jitter or motion-vector scale values");
		return false;
	}

	xefg_swapchain_frame_constant_data_t constants{};
	std::memcpy(constants.viewMatrix, frame.viewMatrix, sizeof(constants.viewMatrix));
	std::memcpy(constants.projectionMatrix, frame.projectionMatrix, sizeof(constants.projectionMatrix));
	constants.jitterOffsetX = std::clamp(frame.jitterOffsetX, -0.5f, 0.5f);
	constants.jitterOffsetY = std::clamp(frame.jitterOffsetY, -0.5f, 0.5f);
	constants.motionVectorScaleX = frame.motionVectorScaleX;
	constants.motionVectorScaleY = frame.motionVectorScaleY;
	constants.resetHistory = (frame.resetHistory || forceHistoryReset_) ? 1U : 0U;
	constants.frameRenderTime = std::isfinite(frame.frameRenderTimeMs) && frame.frameRenderTimeMs >= 0.0f ?
	                                frame.frameRenderTimeMs :
	                                0.0f;
	success &= CheckXeFGResult(
		xefg_.tagFrameConstants(xefgContext_, currentPresentId_, &constants),
		"tag frame constants");

	if (success) {
		taggedThisFrame_ = true;
		forceHistoryReset_ = false;
	}
	return success;
}

bool IntelXeSSFrameGeneration::BeforePresent()
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xefgContext_ || !xellContext_)
		return false;

	if (frameStage_ == FrameStage::Idle) {
		if (!warnedMissingBeginFrame_) {
			warnedMissingBeginFrame_ = true;
			logger::warn("[XeSS-FG] BeforePresent ran without BeginFrame; this present will not have a valid XeLL frame sequence");
		}
		return false;
	}

	bool success = true;
	if (frameStage_ == FrameStage::Simulation) {
		success &= AddMarkerUnlocked(XELL_SIMULATION_END);
		success &= AddMarkerUnlocked(XELL_RENDERSUBMIT_START);
		frameStage_ = FrameStage::RenderSubmit;
	}
	if (frameStage_ != FrameStage::RenderSubmit)
		return false;

	success &= AddMarkerUnlocked(XELL_RENDERSUBMIT_END);
	if (enabled_) {
		if (!taggedThisFrame_ && !warnedUntaggedPresent_) {
			warnedUntaggedPresent_ = true;
			logger::warn("[XeSS-FG] Presenting an enabled frame without complete XeSS-FG inputs; interpolation may be skipped");
		}
		success &= CheckXeFGResult(xefg_.setPresentId(xefgContext_, currentPresentId_), "set present ID");
	}
	success &= AddMarkerUnlocked(XELL_PRESENT_START);
	frameStage_ = FrameStage::Present;
	return success;
}

bool IntelXeSSFrameGeneration::AfterPresent(HRESULT presentResult)
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xefgContext_ || !xellContext_)
		return false;
	if (frameStage_ != FrameStage::Present) {
		logger::error("[XeSS-FG] AfterPresent called without a matching BeforePresent");
		return false;
	}

	bool success = AddMarkerUnlocked(XELL_PRESENT_END);
	lastPresentStatus_ = {};
	const xefg_swapchain_result_t statusResult = xefg_.getLastPresentStatus(xefgContext_, &lastPresentStatus_);
	if (!CheckXeFGResult(statusResult, "query last present status"))
		success = false;

	const int32_t frameGenResult = static_cast<int32_t>(lastPresentStatus_.frameGenResult);
	if (frameGenResult != XEFG_SWAPCHAIN_RESULT_SUCCESS && frameGenResult != lastLoggedFrameGenResult_) {
		if (frameGenResult < 0) {
			logger::error("[XeSS-FG] Interpolation for present {} failed: {} ({})",
				currentPresentId_,
				XeFGResultName(lastPresentStatus_.frameGenResult),
				frameGenResult);
		} else {
			logger::warn("[XeSS-FG] Interpolation for present {} returned: {} ({})",
				currentPresentId_,
				XeFGResultName(lastPresentStatus_.frameGenResult),
				frameGenResult);
		}
	}
	lastLoggedFrameGenResult_ = frameGenResult;
	active_ = SUCCEEDED(presentResult) && enabled_ && lastPresentStatus_.isFrameGenEnabled != 0;
	if (FAILED(presentResult)) {
		logger::error("[XeSS-FG] Proxy Present failed with HRESULT 0x{:08X}", static_cast<uint32_t>(presentResult));
		success = false;
	}

	frameStage_ = FrameStage::Idle;
	taggedThisFrame_ = false;
	if (!globals::state)
		++fallbackGlobalsFrame_;
	return success;
}

xefg_swapchain_present_status_t IntelXeSSFrameGeneration::GetLastPresentStatus() const
{
	std::scoped_lock lock(mutex_);
	return lastPresentStatus_;
}

bool IntelXeSSFrameGeneration::IsEnabled() const
{
	std::scoped_lock lock(mutex_);
	return enabled_;
}

bool IntelXeSSFrameGeneration::IsActive() const
{
	std::scoped_lock lock(mutex_);
	return active_;
}

bool IntelXeSSFrameGeneration::UsesUiComposition() const
{
	std::scoped_lock lock(mutex_);
	return uiCompositionEnabled_;
}

bool IntelXeSSFrameGeneration::OnResize(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
	std::scoped_lock lock(mutex_);
	if (!initialized_ || !xefgContext_)
		return false;

	const uint32_t effectiveWidth = width ? width : displayWidth_;
	const uint32_t effectiveHeight = height ? height : displayHeight_;
	const DXGI_FORMAT effectiveFormat = format != DXGI_FORMAT_UNKNOWN ? format : swapChainFormat_;
	if (effectiveWidth == 0 || effectiveHeight == 0 || effectiveFormat == DXGI_FORMAT_UNKNOWN)
		return false;

	if (!UpdateUiCompositionUnlocked(false))
		return false;

	xefg_swapchain_properties_t resizedProperties{};
	if (!CheckXeFGResult(
			xefg_.d3d12GetProperties(
				xefgContext_, nullptr, effectiveWidth, effectiveHeight, effectiveFormat, &resizedProperties),
			"query resize properties")) {
		return false;
	}

	displayWidth_ = effectiveWidth;
	displayHeight_ = effectiveHeight;
	swapChainFormat_ = effectiveFormat;
	properties_ = resizedProperties;
	forceHistoryReset_ = true;
	active_ = false;
	return true;
}

bool IntelXeSSFrameGeneration::DestroyContextsUnlocked(bool unloadModules)
{
	initialized_ = false;
	active_ = false;
	frameStage_ = FrameStage::Idle;
	taggedThisFrame_ = false;

	if (xefgContext_ && enabled_ && xefg_.setEnabled)
		CheckXeFGResult(xefg_.setEnabled(xefgContext_, 0), "disable frame generation during shutdown");
	enabled_ = false;

	if (xellContext_ && xellLowLatencyEnabled_ && xell_.setSleepMode) {
		xell_sleep_params_t disabledParams{};
		CheckXeLLResult(xell_.setSleepMode(xellContext_, &disabledParams), "disable latency reduction during shutdown");
	}
	xellLowLatencyEnabled_ = false;
	minimumIntervalUs_ = 0;

	// XeSS-FG requires every application-held proxy reference to be gone before context destruction.
	swapChain_ = nullptr;
	if (xefgContext_) {
		const xefg_swapchain_result_t result = xefg_.destroy(xefgContext_);
		if (!CheckXeFGResult(result, "destroy context")) {
			logger::error("[XeSS-FG] Context remains loaded; release all external proxy/backbuffer references and retry Shutdown");
			return false;
		}
		xefgContext_ = nullptr;
	}

	if (xellContext_) {
		const xell_result_t result = xell_.destroy(xellContext_);
		if (!CheckXeLLResult(result, "destroy context"))
			return false;
		xellContext_ = nullptr;
	}

	device_ = nullptr;
	commandQueue_ = nullptr;
	properties_ = {};
	lastPresentStatus_ = {};
	swapChainFormat_ = DXGI_FORMAT_UNKNOWN;
	displayWidth_ = 0;
	displayHeight_ = 0;
	maxInterpolatedFrames_ = 0;
	numInterpolatedFrames_ = 1;
	uiCompositionSupported_ = false;
	uiCompositionEnabled_ = false;
	forceHistoryReset_ = true;
	lastGlobalsFrame_ = std::numeric_limits<uint64_t>::max();
	fallbackGlobalsFrame_ = 0;
	nextPresentId_ = 1;
	currentPresentId_ = 0;
	warnedMissingBeginFrame_ = false;
	warnedIncompleteFrame_ = false;
	warnedUntaggedPresent_ = false;
	lastLoggedFrameGenResult_ = XEFG_SWAPCHAIN_RESULT_SUCCESS;

	if (unloadModules)
		UnloadModulesUnlocked();
	return true;
}

bool IntelXeSSFrameGeneration::Shutdown()
{
	std::scoped_lock lock(mutex_);
	return DestroyContextsUnlocked(true);
}
