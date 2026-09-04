#include "IntelXeSSD3D12.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <type_traits>

#include "IntelXeSS.h"

namespace
{
	constexpr float kJitterLimit = 0.5f;
	constexpr float kJitterEpsilon = 1.0e-5f;

	bool IsKnownQuality(xess_quality_settings_t a_quality)
	{
		switch (a_quality) {
		case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
		case XESS_QUALITY_SETTING_PERFORMANCE:
		case XESS_QUALITY_SETTING_BALANCED:
		case XESS_QUALITY_SETTING_QUALITY:
		case XESS_QUALITY_SETTING_ULTRA_QUALITY:
		case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
		case XESS_QUALITY_SETTING_AA:
			return true;
		default:
			return false;
		}
	}

	void XeSSLoggingCallback(const char* a_message, xess_logging_level_t a_level)
	{
		if (!a_message)
			return;
		std::string message(a_message);
		while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
			message.pop_back();
		if (message.empty())
			return;

		switch (a_level) {
		case XESS_LOGGING_LEVEL_DEBUG:
			logger::debug("[XeSS-SR D3D12 SDK] {}", message);
			break;
		case XESS_LOGGING_LEVEL_INFO:
			logger::info("[XeSS-SR D3D12 SDK] {}", message);
			break;
		case XESS_LOGGING_LEVEL_WARNING:
			logger::warn("[XeSS-SR D3D12 SDK] {}", message);
			break;
		case XESS_LOGGING_LEVEL_ERROR:
		default:
			logger::error("[XeSS-SR D3D12 SDK] {}", message);
			break;
		}
	}
}

IntelXeSSD3D12::~IntelXeSSD3D12()
{
	Shutdown();
}

void IntelXeSSD3D12::Api::Reset()
{
	getVersion = nullptr;
	getIntelXeFXVersion = nullptr;
	createContext = nullptr;
	buildPipelines = nullptr;
	initializeContext = nullptr;
	execute = nullptr;
	getOptimalInputResolution = nullptr;
	setVelocityScale = nullptr;
	setLoggingCallback = nullptr;
	isOptimalDriver = nullptr;
	destroyContext = nullptr;
}

bool IntelXeSSD3D12::Load()
{
	if (module_)
		return true;
	if (triedLoad)
		return false;

	triedLoad = true;
	available = false;
	initialized = false;
	oldDriverWarning = false;

	std::filesystem::path dllPath = std::filesystem::path(PluginDir) / DllName;
	std::error_code pathError;
	auto absolutePath = std::filesystem::absolute(dllPath, pathError);
	if (!pathError)
		dllPath = std::move(absolutePath);

	module_ = LoadLibraryExW(
		dllPath.c_str(),
		nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!module_) {
		logger::info(
			"[XeSS-SR D3D12] Optional cross-vendor runtime was not loaded from {} (Win32 error 0x{:08X})",
			dllPath.string(),
			GetLastError());
		return false;
	}

	if (!ResolveApi()) {
		FreeLibrary(module_);
		module_ = nullptr;
		api_.Reset();
		return false;
	}

	xess_version_t version{};
	if (LogResult("xessGetVersion", api_.getVersion(&version))) {
		logger::info(
			"[XeSS-SR D3D12] Loaded SDK API version {}.{}.{} from {}",
			version.major,
			version.minor,
			version.patch,
			dllPath.string());
	}
	return true;
}

bool IntelXeSSD3D12::ResolveApi()
{
	std::vector<std::string> missingExports;
	const auto bindRequired = [this, &missingExports](auto& a_function, const char* a_name) {
		using FunctionType = std::remove_reference_t<decltype(a_function)>;
		a_function = reinterpret_cast<FunctionType>(GetProcAddress(module_, a_name));
		if (!a_function)
			missingExports.emplace_back(a_name);
	};

	bindRequired(api_.getVersion, "xessGetVersion");
	bindRequired(api_.createContext, "xessD3D12CreateContext");
	bindRequired(api_.buildPipelines, "xessD3D12BuildPipelines");
	bindRequired(api_.initializeContext, "xessD3D12Init");
	bindRequired(api_.execute, "xessD3D12Execute");
	bindRequired(api_.getOptimalInputResolution, "xessGetOptimalInputResolution");
	bindRequired(api_.setVelocityScale, "xessSetVelocityScale");
	bindRequired(api_.setLoggingCallback, "xessSetLoggingCallback");
	bindRequired(api_.isOptimalDriver, "xessIsOptimalDriver");
	bindRequired(api_.destroyContext, "xessDestroyContext");
	api_.getIntelXeFXVersion = reinterpret_cast<GetIntelXeFXVersionFn>(GetProcAddress(module_, "xessGetIntelXeFXVersion"));

	if (missingExports.empty())
		return true;

	std::string exports;
	for (const auto& name : missingExports) {
		if (!exports.empty())
			exports += ", ";
		exports += name;
	}
	logger::error("[XeSS-SR D3D12] Runtime is incompatible; required exports are missing: {}", exports);
	return false;
}

bool IntelXeSSD3D12::Initialize(ID3D12Device* a_device)
{
	if (!a_device || !Load())
		return false;
	if (context_ && device_.get() == a_device)
		return true;
	if (context_) {
		AdoptCallingThread("Initialize");
		if (!DestroyResources())
			return false;
	}

	// Same thread model as IntelXeSS::Initialize: the device is bound on the main thread, but
	// every SDK call happens on the render thread, so the context is created there on first
	// use and that thread becomes the owner. Creating it here made every later call fail.
	device_.copy_from(a_device);
	ownerThread_ = {};
	available = true;
	initialized = false;
	oldDriverWarning = false;
	ResetConfiguration();
	logger::info("[XeSS-SR D3D12] Runtime bound; the context is created on first use by the render thread");
	return true;
}

bool IntelXeSSD3D12::EnsureContext()
{
	if (context_)
		return true;
	if (!module_ || !device_ || !api_.createContext)
		return false;
	// First caller after Initialize(device) adopts ownership; see Initialize.
	AdoptCallingThread("xessD3D12CreateContext");

	xess_context_handle_t newContext = nullptr;
	const auto result = api_.createContext(device_.get(), &newContext);
	if (result < XESS_RESULT_SUCCESS || !newContext) {
		LogResult("xessD3D12CreateContext", result);
		available = false;
		return false;
	}

	context_ = newContext;
	available = true;
	LogResult("xessD3D12CreateContext", result);
	LogResult("xessSetLoggingCallback", api_.setLoggingCallback(context_, XESS_LOGGING_LEVEL_WARNING, XeSSLoggingCallback));

	const auto driverResult = api_.isOptimalDriver(context_);
	if (driverResult == XESS_RESULT_WARNING_OLD_DRIVER) {
		oldDriverWarning = true;
		logger::warn("[XeSS-SR D3D12] The graphics driver is supported but not optimal; an update is recommended");
	} else {
		LogResult("xessIsOptimalDriver", driverResult);
	}

	logger::info("[XeSS-SR D3D12] Cross-vendor context created successfully");
	return true;
}

bool IntelXeSSD3D12::CreateResources(
	const xess_2d_t& a_outputResolution,
	xess_quality_settings_t a_quality,
	bool a_useResponsiveMask,
	bool a_useAutoExposure)
{
	if (!a_outputResolution.x || !a_outputResolution.y || !IsKnownQuality(a_quality) ||
		!EnsureContext()) {
		return false;
	}
	AdoptCallingThread("xessD3D12Init");

	uint32_t initFlags = XESS_INIT_FLAG_NONE;
	if (a_useResponsiveMask)
		initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
	if (a_useAutoExposure)
		initFlags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

	if (initialized && outputResolution_.x == a_outputResolution.x && outputResolution_.y == a_outputResolution.y &&
		quality_ == a_quality && initFlags_ == initFlags) {
		return true;
	}

	InputResolutionRange inputRange{};
	if (!QueryOptimalInputResolution(a_outputResolution, a_quality, inputRange))
		return false;

	if (!initialized && !LogResult(
							"xessD3D12BuildPipelines",
							api_.buildPipelines(context_, nullptr, false, initFlags))) {
		return false;
	}

	xess_d3d12_init_params_t initParams{};
	initParams.outputResolution = a_outputResolution;
	initParams.qualitySetting = a_quality;
	initParams.initFlags = initFlags;
	initParams.creationNodeMask = 0;
	initParams.visibleNodeMask = 0;
	if (!LogResult("xessD3D12Init", api_.initializeContext(context_, &initParams))) {
		initialized = false;
		return false;
	}

	outputResolution_ = a_outputResolution;
	inputResolutionRange_ = inputRange;
	quality_ = a_quality;
	initFlags_ = initFlags;
	useResponsiveMask_ = a_useResponsiveMask;
	useAutoExposure_ = a_useAutoExposure;
	velocityScaleX_ = 0.0f;
	velocityScaleY_ = 0.0f;
	lastExecuteResult_ = XESS_RESULT_SUCCESS;
	executeResultLogged_ = false;
	initialized = true;

	// Same post-init check as the D3D11 wrapper: XeFX (Intel's XMX kernels) only reports a
	// version once the model is loaded, so the answer is meaningful only here.
	if (api_.getIntelXeFXVersion) {
		xess_version_t xefxVersion{};
		if (LogResult("xessGetIntelXeFXVersion", api_.getIntelXeFXVersion(context_, &xefxVersion))) {
			const bool xefxLoaded = xefxVersion.major || xefxVersion.minor || xefxVersion.patch;
			logger::info("[XeSS-SR D3D12] Intel XeFX runtime after init: {}.{}.{} ({})",
				xefxVersion.major,
				xefxVersion.minor,
				xefxVersion.patch,
				xefxLoaded ? "XMX-accelerated path" : "not loaded; generic DP4a path");
		}
	}

	logger::info(
		"[XeSS-SR D3D12] Initialized {} at {}x{} (optimal input {}x{}, range {}x{}-{}x{})",
		IntelXeSS::QualityToString(a_quality),
		a_outputResolution.x,
		a_outputResolution.y,
		inputRange.optimal.x,
		inputRange.optimal.y,
		inputRange.minimum.x,
		inputRange.minimum.y,
		inputRange.maximum.x,
		inputRange.maximum.y);
	return true;
}

bool IntelXeSSD3D12::QueryOptimalInputResolution(
	const xess_2d_t& a_outputResolution,
	xess_quality_settings_t a_quality,
	InputResolutionRange& a_result)
{
	a_result = {};
	if (!a_outputResolution.x || !a_outputResolution.y || !IsKnownQuality(a_quality) ||
		!EnsureContext()) {
		return false;
	}
	AdoptCallingThread("xessGetOptimalInputResolution");
	return LogResult(
		"xessGetOptimalInputResolution",
		api_.getOptimalInputResolution(
			context_, &a_outputResolution, a_quality, &a_result.optimal, &a_result.minimum, &a_result.maximum));
}

bool IntelXeSSD3D12::Upscale(
	ID3D12GraphicsCommandList* a_commandList,
	ID3D12Resource* a_color,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	ID3D12Resource* a_responsiveMask,
	ID3D12Resource* a_output,
	uint32_t a_inputWidth,
	uint32_t a_inputHeight,
	float a_jitterOffsetX,
	float a_jitterOffsetY,
	bool a_resetHistory,
	float a_exposureScale)
{
	if (!initialized || !context_ || !a_commandList)
		return false;
	AdoptCallingThread("xessD3D12Execute");
	if (!std::isfinite(a_jitterOffsetX) || !std::isfinite(a_jitterOffsetY) ||
		std::abs(a_jitterOffsetX) > kJitterLimit + kJitterEpsilon ||
		std::abs(a_jitterOffsetY) > kJitterLimit + kJitterEpsilon ||
		!std::isfinite(a_exposureScale) || a_exposureScale <= 0.0f) {
		logger::error("[XeSS-SR D3D12] Invalid jitter or exposure scale");
		return false;
	}
	if (!ValidateExecutionResources(
			a_color, a_depth, a_motionVectors, a_responsiveMask, a_output, a_inputWidth, a_inputHeight)) {
		return false;
	}

	const float velocityScaleX = static_cast<float>(a_inputWidth);
	const float velocityScaleY = static_cast<float>(a_inputHeight);
	if (velocityScaleX_ != velocityScaleX || velocityScaleY_ != velocityScaleY) {
		if (!LogResult("xessSetVelocityScale", api_.setVelocityScale(context_, velocityScaleX, velocityScaleY)))
			return false;
		velocityScaleX_ = velocityScaleX;
		velocityScaleY_ = velocityScaleY;
	}

	xess_d3d12_execute_params_t params{};
	params.pColorTexture = a_color;
	params.pVelocityTexture = a_motionVectors;
	params.pDepthTexture = a_depth;
	params.pResponsivePixelMaskTexture = useResponsiveMask_ ? a_responsiveMask : nullptr;
	params.pOutputTexture = a_output;
	params.jitterOffsetX = a_jitterOffsetX;
	params.jitterOffsetY = a_jitterOffsetY;
	params.exposureScale = a_exposureScale;
	params.resetHistory = a_resetHistory ? 1u : 0u;
	params.inputWidth = a_inputWidth;
	params.inputHeight = a_inputHeight;

	const auto result = api_.execute(context_, a_commandList, &params);
	if (result != XESS_RESULT_SUCCESS) {
		if (!executeResultLogged_ || lastExecuteResult_ != result) {
			LogResult("xessD3D12Execute", result);
			executeResultLogged_ = true;
			lastExecuteResult_ = result;
		}
		return result > XESS_RESULT_SUCCESS;
	}
	executeResultLogged_ = false;
	lastExecuteResult_ = result;
	return true;
}

bool IntelXeSSD3D12::ValidateExecutionResources(
	ID3D12Resource* a_color,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	ID3D12Resource* a_responsiveMask,
	ID3D12Resource* a_output,
	uint32_t a_inputWidth,
	uint32_t a_inputHeight) const
{
	if (!a_color || !a_depth || !a_motionVectors || !a_output || (useResponsiveMask_ && !a_responsiveMask) ||
		a_color == a_output || !a_inputWidth || !a_inputHeight) {
		logger::error("[XeSS-SR D3D12] Required execution textures are missing or invalid");
		return false;
	}
	if (a_inputWidth < inputResolutionRange_.minimum.x || a_inputHeight < inputResolutionRange_.minimum.y ||
		a_inputWidth > inputResolutionRange_.maximum.x || a_inputHeight > inputResolutionRange_.maximum.y) {
		logger::error("[XeSS-SR D3D12] Input {}x{} is outside the supported range", a_inputWidth, a_inputHeight);
		return false;
	}

	const auto colorDesc = a_color->GetDesc();
	const auto depthDesc = a_depth->GetDesc();
	const auto motionDesc = a_motionVectors->GetDesc();
	const auto outputDesc = a_output->GetDesc();
	if (colorDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		depthDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		motionDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		outputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		colorDesc.Width < a_inputWidth || colorDesc.Height < a_inputHeight ||
		depthDesc.Width < a_inputWidth || depthDesc.Height < a_inputHeight ||
		motionDesc.Width < a_inputWidth || motionDesc.Height < a_inputHeight ||
		outputDesc.Width < outputResolution_.x || outputDesc.Height < outputResolution_.y ||
		colorDesc.Format != outputDesc.Format ||
		(outputDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
		logger::error("[XeSS-SR D3D12] Execution texture descriptions are incompatible");
		return false;
	}
	if (motionDesc.Format != DXGI_FORMAT_R16G16_FLOAT && motionDesc.Format != DXGI_FORMAT_R16G16_TYPELESS) {
		logger::error("[XeSS-SR D3D12] Motion vectors must use R16G16_FLOAT");
		return false;
	}
	return true;
}

bool IntelXeSSD3D12::DestroyResources()
{
	if (!context_) {
		initialized = false;
		ResetConfiguration();
		return true;
	}
	AdoptCallingThread("xessDestroyContext");
	if (!LogResult("xessDestroyContext", api_.destroyContext(context_)))
		return false;
	context_ = nullptr;
	initialized = false;
	ResetConfiguration();
	return true;
}

bool IntelXeSSD3D12::Shutdown()
{
	if (context_ && !DestroyResources())
		return false;
	device_ = nullptr;
	ownerThread_ = {};
	available = false;
	initialized = false;
	oldDriverWarning = false;
	ResetConfiguration();
	if (module_) {
		if (!FreeLibrary(module_))
			return false;
		module_ = nullptr;
	}
	api_.Reset();
	triedLoad = false;
	return true;
}

void IntelXeSSD3D12::AdoptCallingThread(const char* a_operation) const
{
	const auto thisThread = std::this_thread::get_id();
	if (ownerThread_ == thisThread)
		return;
	// Same reasoning as IntelXeSS::AdoptCallingThread: the engine moves these calls between the
	// render and main threads, and only serializes them, so adopt instead of refusing.
	if (ownerThread_ != std::thread::id{})
		logger::info("[XeSS-SR D3D12] {} moved to another thread; adopting it as the calling thread", a_operation);
	ownerThread_ = thisThread;
}

bool IntelXeSSD3D12::LogResult(const char* a_operation, xess_result_t a_result, bool a_warningIsSuccess) const
{
	if (a_result == XESS_RESULT_SUCCESS)
		return true;
	if (a_result > XESS_RESULT_SUCCESS) {
		logger::warn("[XeSS-SR D3D12] {} returned {} ({})", a_operation, IntelXeSS::ResultToString(a_result), static_cast<int>(a_result));
		return a_warningIsSuccess;
	}
	logger::error("[XeSS-SR D3D12] {} failed with {} ({})", a_operation, IntelXeSS::ResultToString(a_result), static_cast<int>(a_result));
	return false;
}

void IntelXeSSD3D12::ResetConfiguration()
{
	outputResolution_ = {};
	inputResolutionRange_ = {};
	quality_ = XESS_QUALITY_SETTING_QUALITY;
	initFlags_ = XESS_INIT_FLAG_NONE;
	useResponsiveMask_ = false;
	useAutoExposure_ = false;
	velocityScaleX_ = 0.0f;
	velocityScaleY_ = 0.0f;
	lastExecuteResult_ = XESS_RESULT_SUCCESS;
	executeResultLogged_ = false;
}
