#include "IntelXeSS.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <type_traits>

#include "../../Utils/FileSystem.h"

namespace
{
	constexpr float kJitterLimit = 0.5f;
	constexpr float kJitterEpsilon = 1.0e-5f;

	std::string FormatVersion(const xess_version_t& a_version)
	{
		return std::to_string(a_version.major) + "." +
		       std::to_string(a_version.minor) + "." +
		       std::to_string(a_version.patch);
	}

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

	bool GetTextureDescription(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_description)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&a_description);
		return true;
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
			logger::debug("[XeSS-SR SDK] {}", message);
			break;
		case XESS_LOGGING_LEVEL_INFO:
			logger::info("[XeSS-SR SDK] {}", message);
			break;
		case XESS_LOGGING_LEVEL_WARNING:
			logger::warn("[XeSS-SR SDK] {}", message);
			break;
		case XESS_LOGGING_LEVEL_ERROR:
		default:
			logger::error("[XeSS-SR SDK] {}", message);
			break;
		}
	}
}

std::vector<std::pair<std::string, std::string>> IntelXeSS::dllVersions = {};

IntelXeSS::~IntelXeSS()
{
	Shutdown();
}

void IntelXeSS::Api::Reset()
{
	getVersion = nullptr;
	getIntelXeFXVersion = nullptr;
	createContext = nullptr;
	initializeContext = nullptr;
	execute = nullptr;
	getOptimalInputResolution = nullptr;
	setVelocityScale = nullptr;
	setLoggingCallback = nullptr;
	isOptimalDriver = nullptr;
	destroyContext = nullptr;
}

bool IntelXeSS::Load()
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

	dllVersions.clear();
	std::error_code directoryError;
	if (std::filesystem::exists(dllPath.parent_path(), directoryError) && !directoryError) {
		dllVersions = Util::EnumerateDllVersions(dllPath.parent_path());
		for (const auto& [name, version] : dllVersions)
			logger::info("[XeSS-SR] {} version: {}", name, version);
	}

	module_ = LoadLibraryExW(
		dllPath.c_str(),
		nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!module_) {
		const DWORD error = GetLastError();
		logger::info(
			"[XeSS-SR] Optional D3D11 runtime was not loaded from {} (Win32 error 0x{:08X})",
			dllPath.string(),
			error);
		return false;
	}

	if (!ResolveApi()) {
		FreeLibrary(module_);
		module_ = nullptr;
		api_.Reset();
		return false;
	}

	sdkVersion_ = {};
	const auto versionResult = api_.getVersion(&sdkVersion_);
	if (LogResult("xessGetVersion", versionResult)) {
		logger::info("[XeSS-SR] Loaded SDK API version {} from {}", FormatVersion(sdkVersion_), dllPath.string());
	}

	return true;
}

bool IntelXeSS::ResolveApi()
{
	if (!module_)
		return false;

	std::vector<std::string> missingExports;
	const auto bindRequired = [this, &missingExports](auto& a_function, const char* a_name) {
		using FunctionType = std::remove_reference_t<decltype(a_function)>;
		a_function = reinterpret_cast<FunctionType>(GetProcAddress(module_, a_name));
		if (!a_function)
			missingExports.emplace_back(a_name);
	};

	bindRequired(api_.getVersion, "xessGetVersion");
	bindRequired(api_.createContext, "xessD3D11CreateContext");
	bindRequired(api_.initializeContext, "xessD3D11Init");
	bindRequired(api_.execute, "xessD3D11Execute");
	bindRequired(api_.getOptimalInputResolution, "xessGetOptimalInputResolution");
	bindRequired(api_.setVelocityScale, "xessSetVelocityScale");
	bindRequired(api_.setLoggingCallback, "xessSetLoggingCallback");
	bindRequired(api_.isOptimalDriver, "xessIsOptimalDriver");
	bindRequired(api_.destroyContext, "xessDestroyContext");

	api_.getIntelXeFXVersion = reinterpret_cast<GetIntelXeFXVersionFn>(GetProcAddress(module_, "xessGetIntelXeFXVersion"));
	if (!api_.getIntelXeFXVersion)
		logger::warn("[XeSS-SR] Runtime does not export optional xessGetIntelXeFXVersion diagnostics");

	if (missingExports.empty())
		return true;

	std::string exports;
	for (const auto& name : missingExports) {
		if (!exports.empty())
			exports += ", ";
		exports += name;
	}
	logger::error("[XeSS-SR] Runtime is incompatible; required exports are missing: {}", exports);
	return false;
}

bool IntelXeSS::Initialize(ID3D11Device* a_device)
{
	if (!a_device) {
		logger::error("[XeSS-SR] Cannot initialize without a D3D11 device");
		return false;
	}
	if (!Load())
		return false;

	if (context_ && device_.get() == a_device)
		return true;

	if (context_) {
		if (!CheckOwnerThread("Initialize") || !DestroyResources())
			return false;
	}

	// The SDK requires every XeSS-SR call to come from the thread that initialized it. Devices
	// are created on the game's main thread, but xessD3D11Init/Execute/Destroy all run on the
	// render thread, so creating the context here pinned ownership to a thread that never
	// calls it again and every later call was refused. Only bind the device now; the first
	// CreateResources/Upscale call creates the context and becomes the owning thread.
	device_.copy_from(a_device);
	ownerThread_ = {};
	available = true;
	initialized = false;
	oldDriverWarning = false;
	ResetConfiguration();

	logger::info("[XeSS-SR] Native D3D11 runtime bound; the context is created on first use by the render thread");
	return true;
}

bool IntelXeSS::Initialize(
	ID3D11Device* a_device,
	const xess_2d_t& a_outputResolution,
	xess_quality_settings_t a_quality,
	bool a_useResponsiveMask,
	bool a_useAutoExposure)
{
	return Initialize(a_device) &&
	       CreateResources(a_outputResolution, a_quality, a_useResponsiveMask, a_useAutoExposure);
}

bool IntelXeSS::EnsureContext()
{
	if (context_)
		return true;
	if (!module_ || !device_ || !api_.createContext) {
		logger::error("[XeSS-SR] Cannot create a context before loading the runtime and binding a D3D11 device");
		return false;
	}
	// First caller after Initialize(device) adopts ownership; see Initialize.
	if (ownerThread_ == std::thread::id{})
		ownerThread_ = std::this_thread::get_id();
	else if (!CheckOwnerThread("xessD3D11CreateContext"))
		return false;

	xess_context_handle_t newContext = nullptr;
	const auto result = api_.createContext(device_.get(), &newContext);
	if (result < XESS_RESULT_SUCCESS || !newContext) {
		if (result == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE || result == XESS_RESULT_ERROR_UNSUPPORTED_DRIVER) {
			logger::info(
				"[XeSS-SR] Native D3D11 XeSS-SR is unavailable on this adapter: {}",
				ResultToString(result));
		} else if (result >= XESS_RESULT_SUCCESS) {
			logger::error("[XeSS-SR] xessD3D11CreateContext returned no context ({})", ResultToString(result));
		} else {
			LogResult("xessD3D11CreateContext", result);
		}
		available = false;
		return false;
	}

	context_ = newContext;
	available = true;
	LogResult("xessD3D11CreateContext", result);

	const auto loggingResult = api_.setLoggingCallback(
		context_, XESS_LOGGING_LEVEL_WARNING, XeSSLoggingCallback);
	LogResult("xessSetLoggingCallback", loggingResult);

	const auto driverResult = api_.isOptimalDriver(context_);
	if (driverResult == XESS_RESULT_WARNING_OLD_DRIVER) {
		oldDriverWarning = true;
		logger::warn("[XeSS-SR] The installed graphics driver is supported but not optimal; an update is recommended");
	} else {
		LogResult("xessIsOptimalDriver", driverResult);
	}

	intelXeFXVersion_ = {};
	if (api_.getIntelXeFXVersion) {
		const auto xefxResult = api_.getIntelXeFXVersion(context_, &intelXeFXVersion_);
		if (LogResult("xessGetIntelXeFXVersion", xefxResult))
			logger::info("[XeSS-SR] Intel XeFX runtime version: {}", FormatVersion(intelXeFXVersion_));
	}

	logger::info("[XeSS-SR] Native D3D11 context created successfully");
	return true;
}

bool IntelXeSS::CreateResources(
	const xess_2d_t& a_outputResolution,
	xess_quality_settings_t a_quality,
	bool a_useResponsiveMask,
	bool a_useAutoExposure)
{
	if (a_outputResolution.x == 0 || a_outputResolution.y == 0) {
		logger::error("[XeSS-SR] Output resolution must be non-zero");
		return false;
	}
	if (!IsKnownQuality(a_quality)) {
		logger::error("[XeSS-SR] Invalid quality setting {}", static_cast<int>(a_quality));
		return false;
	}
	uint32_t initFlags = XESS_INIT_FLAG_NONE;
	if (a_useResponsiveMask)
		initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
	if (a_useAutoExposure)
		initFlags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

	// Called every frame; the no-op case must not touch the SDK or its thread bookkeeping.
	if (initialized && context_ &&
		outputResolution_.x == a_outputResolution.x &&
		outputResolution_.y == a_outputResolution.y &&
		quality_ == a_quality &&
		initFlags_ == initFlags) {
		return true;
	}

	if (!EnsureContext() || !CheckOwnerThread("xessD3D11Init"))
		return false;

	InputResolutionRange inputRange{};
	if (!QueryOptimalInputResolution(a_outputResolution, a_quality, inputRange))
		return false;

	xess_d3d11_init_params_t initParams{};
	initParams.outputResolution = a_outputResolution;
	initParams.qualitySetting = a_quality;
	initParams.initFlags = initFlags;

	const auto result = api_.initializeContext(context_, &initParams);
	if (!LogResult("xessD3D11Init", result)) {
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

	logger::info(
		"[XeSS-SR] Initialized {} at {}x{} (optimal input {}x{}, range {}x{}-{}x{}, responsive mask={}, auto exposure={})",
		QualityToString(a_quality),
		a_outputResolution.x,
		a_outputResolution.y,
		inputRange.optimal.x,
		inputRange.optimal.y,
		inputRange.minimum.x,
		inputRange.minimum.y,
		inputRange.maximum.x,
		inputRange.maximum.y,
		a_useResponsiveMask,
		a_useAutoExposure);
	return true;
}

bool IntelXeSS::QueryOptimalInputResolution(
	const xess_2d_t& a_outputResolution,
	xess_quality_settings_t a_quality,
	InputResolutionRange& a_result)
{
	a_result = {};
	if (a_outputResolution.x == 0 || a_outputResolution.y == 0 || !IsKnownQuality(a_quality)) {
		logger::error("[XeSS-SR] Invalid output resolution or quality passed to xessGetOptimalInputResolution");
		return false;
	}
	if (!EnsureContext() || !CheckOwnerThread("xessGetOptimalInputResolution"))
		return false;

	const auto result = api_.getOptimalInputResolution(
		context_,
		&a_outputResolution,
		a_quality,
		&a_result.optimal,
		&a_result.minimum,
		&a_result.maximum);
	return LogResult("xessGetOptimalInputResolution", result);
}

bool IntelXeSS::Upscale(
	ID3D11Resource* a_color,
	ID3D11Resource* a_depth,
	ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_responsiveMask,
	ID3D11Resource* a_output,
	uint32_t a_inputWidth,
	uint32_t a_inputHeight,
	float a_jitterOffsetX,
	float a_jitterOffsetY,
	bool a_resetHistory,
	float a_exposureScale)
{
	if (!initialized || !context_) {
		logger::error("[XeSS-SR] Upscale called before successful initialization");
		return false;
	}
	if (!CheckOwnerThread("xessD3D11Execute"))
		return false;
	if (!std::isfinite(a_jitterOffsetX) || !std::isfinite(a_jitterOffsetY) ||
		std::abs(a_jitterOffsetX) > kJitterLimit + kJitterEpsilon ||
		std::abs(a_jitterOffsetY) > kJitterLimit + kJitterEpsilon) {
		logger::error(
			"[XeSS-SR] Jitter ({}, {}) is outside the required [-0.5, 0.5] range",
			a_jitterOffsetX,
			a_jitterOffsetY);
		return false;
	}
	if (!std::isfinite(a_exposureScale) || a_exposureScale <= 0.0f) {
		logger::error("[XeSS-SR] Exposure scale must be finite and greater than zero");
		return false;
	}
	if (!ValidateExecutionResources(
			a_color,
			a_depth,
			a_motionVectors,
			a_responsiveMask,
			a_output,
			a_inputWidth,
			a_inputHeight)) {
		return false;
	}

	const float velocityScaleX = static_cast<float>(a_inputWidth);
	const float velocityScaleY = static_cast<float>(a_inputHeight);
	if (velocityScaleX_ != velocityScaleX || velocityScaleY_ != velocityScaleY) {
		const auto scaleResult = api_.setVelocityScale(context_, velocityScaleX, velocityScaleY);
		if (!LogResult("xessSetVelocityScale", scaleResult))
			return false;
		velocityScaleX_ = velocityScaleX;
		velocityScaleY_ = velocityScaleY;
	}

	xess_d3d11_execute_params_t executeParams{};
	executeParams.pColorTexture = a_color;
	executeParams.pVelocityTexture = a_motionVectors;
	executeParams.pDepthTexture = a_depth;
	executeParams.pExposureScaleTexture = nullptr;
	executeParams.pResponsivePixelMaskTexture = useResponsiveMask_ ? a_responsiveMask : nullptr;
	executeParams.pOutputTexture = a_output;
	executeParams.jitterOffsetX = a_jitterOffsetX;
	executeParams.jitterOffsetY = a_jitterOffsetY;
	executeParams.exposureScale = a_exposureScale;
	executeParams.resetHistory = a_resetHistory ? 1u : 0u;
	executeParams.inputWidth = a_inputWidth;
	executeParams.inputHeight = a_inputHeight;

	const auto result = api_.execute(context_, &executeParams);
	if (result != XESS_RESULT_SUCCESS) {
		if (!executeResultLogged_ || lastExecuteResult_ != result) {
			LogResult("xessD3D11Execute", result);
			executeResultLogged_ = true;
			lastExecuteResult_ = result;
		}
		return result > XESS_RESULT_SUCCESS;
	}

	executeResultLogged_ = false;
	lastExecuteResult_ = result;
	return true;
}

bool IntelXeSS::ValidateExecutionResources(
	ID3D11Resource* a_color,
	ID3D11Resource* a_depth,
	ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_responsiveMask,
	ID3D11Resource* a_output,
	uint32_t a_inputWidth,
	uint32_t a_inputHeight) const
{
	if (!a_color || !a_depth || !a_motionVectors || !a_output) {
		logger::error("[XeSS-SR] Color, depth, motion-vector, and output textures are required");
		return false;
	}
	if (useResponsiveMask_ && !a_responsiveMask) {
		logger::error("[XeSS-SR] A responsive mask is required by the active initialization flags");
		return false;
	}
	if (a_color == a_output) {
		logger::error("[XeSS-SR] Input and output color textures must not alias");
		return false;
	}
	if (a_inputWidth == 0 || a_inputHeight == 0) {
		logger::error("[XeSS-SR] Input resolution must be non-zero");
		return false;
	}

	const auto& minimum = inputResolutionRange_.minimum;
	const auto& maximum = inputResolutionRange_.maximum;
	if (a_inputWidth < minimum.x || a_inputHeight < minimum.y ||
		a_inputWidth > maximum.x || a_inputHeight > maximum.y) {
		logger::error(
			"[XeSS-SR] Input {}x{} is outside the supported {}x{}-{}x{} range",
			a_inputWidth,
			a_inputHeight,
			minimum.x,
			minimum.y,
			maximum.x,
			maximum.y);
		return false;
	}

	const double inputAspect = static_cast<double>(a_inputWidth) / static_cast<double>(a_inputHeight);
	const double outputAspect = static_cast<double>(outputResolution_.x) / static_cast<double>(outputResolution_.y);
	const double relativeAspectError = std::abs(inputAspect - outputAspect) / outputAspect;
	if (relativeAspectError > 0.005) {
		logger::error(
			"[XeSS-SR] Input {}x{} must have the same aspect ratio as output {}x{}",
			a_inputWidth,
			a_inputHeight,
			outputResolution_.x,
			outputResolution_.y);
		return false;
	}

	D3D11_TEXTURE2D_DESC colorDesc{};
	D3D11_TEXTURE2D_DESC depthDesc{};
	D3D11_TEXTURE2D_DESC motionDesc{};
	D3D11_TEXTURE2D_DESC outputDesc{};
	if (!GetTextureDescription(a_color, colorDesc) ||
		!GetTextureDescription(a_depth, depthDesc) ||
		!GetTextureDescription(a_motionVectors, motionDesc) ||
		!GetTextureDescription(a_output, outputDesc)) {
		logger::error("[XeSS-SR] All XeSS resources must be D3D11 Texture2D resources");
		return false;
	}

	if (colorDesc.Width < a_inputWidth || colorDesc.Height < a_inputHeight ||
		depthDesc.Width < a_inputWidth || depthDesc.Height < a_inputHeight ||
		motionDesc.Width < a_inputWidth || motionDesc.Height < a_inputHeight) {
		logger::error("[XeSS-SR] One or more input textures are smaller than the declared input resolution");
		return false;
	}
	if (outputDesc.Width < outputResolution_.x || outputDesc.Height < outputResolution_.y) {
		logger::error("[XeSS-SR] Output texture is smaller than the initialized output resolution");
		return false;
	}
	if (colorDesc.Format != outputDesc.Format) {
		logger::error(
			"[XeSS-SR] Input and output color formats must match (input={}, output={})",
			static_cast<uint32_t>(colorDesc.Format),
			static_cast<uint32_t>(outputDesc.Format));
		return false;
	}
	if ((outputDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0) {
		logger::error("[XeSS-SR] Output texture is missing D3D11_BIND_UNORDERED_ACCESS");
		return false;
	}
	if (motionDesc.Format != DXGI_FORMAT_R16G16_FLOAT && motionDesc.Format != DXGI_FORMAT_R16G16_TYPELESS) {
		logger::error(
			"[XeSS-SR] Motion vectors must use R16G16_FLOAT (actual format={})",
			static_cast<uint32_t>(motionDesc.Format));
		return false;
	}

	if (useResponsiveMask_) {
		D3D11_TEXTURE2D_DESC responsiveDesc{};
		if (!GetTextureDescription(a_responsiveMask, responsiveDesc) ||
			responsiveDesc.Width < a_inputWidth || responsiveDesc.Height < a_inputHeight) {
			logger::error("[XeSS-SR] Responsive mask must be a Texture2D covering the input resolution");
			return false;
		}
	}

	return true;
}

bool IntelXeSS::DestroyResources()
{
	if (!context_) {
		initialized = false;
		ResetConfiguration();
		return true;
	}
	if (!CheckOwnerThread("xessDestroyContext"))
		return false;

	const auto result = api_.destroyContext(context_);
	if (!LogResult("xessDestroyContext", result))
		return false;

	context_ = nullptr;
	initialized = false;
	ResetConfiguration();
	logger::info("[XeSS-SR] Context destroyed");
	return true;
}

bool IntelXeSS::Shutdown()
{
	if (context_ && !DestroyResources()) {
		logger::error("[XeSS-SR] Runtime remains loaded because its active context could not be destroyed safely");
		return false;
	}

	device_ = nullptr;
	ownerThread_ = {};
	available = false;
	initialized = false;
	oldDriverWarning = false;
	ResetConfiguration();

	if (module_) {
		if (!FreeLibrary(module_)) {
			logger::error("[XeSS-SR] FreeLibrary failed with Win32 error 0x{:08X}", GetLastError());
			return false;
		}
		module_ = nullptr;
	}

	api_.Reset();
	triedLoad = false;
	return true;
}

bool IntelXeSS::CheckOwnerThread(const char* a_operation) const
{
	const auto thisThread = std::this_thread::get_id();
	if (ownerThread_ == std::thread::id{} || ownerThread_ == thisThread) {
		ownerThread_ = thisThread;
		return true;
	}

	// Skyrim issues the upscaling calls from the render thread in the main menu and from the
	// main thread in the world, so a fixed owner is impossible here. The SDK only forbids
	// concurrent use, which the engine already serializes; the D3D11 immediate context has no
	// thread affinity. Adopt the new thread and leave a trace in the log.
	logger::info("[XeSS-SR] {} moved to another thread; adopting it as the calling thread", a_operation);
	ownerThread_ = thisThread;
	return true;
}

bool IntelXeSS::LogResult(const char* a_operation, xess_result_t a_result, bool a_warningIsSuccess) const
{
	if (a_result == XESS_RESULT_SUCCESS)
		return true;

	if (a_result > XESS_RESULT_SUCCESS) {
		logger::warn("[XeSS-SR] {} returned {} ({})", a_operation, ResultToString(a_result), static_cast<int>(a_result));
		return a_warningIsSuccess;
	}

	logger::error("[XeSS-SR] {} failed with {} ({})", a_operation, ResultToString(a_result), static_cast<int>(a_result));
	return false;
}

void IntelXeSS::ResetConfiguration()
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

const char* IntelXeSS::ResultToString(xess_result_t a_result)
{
	switch (a_result) {
	case XESS_RESULT_WARNING_NONEXISTING_FOLDER:
		return "XESS_RESULT_WARNING_NONEXISTING_FOLDER";
	case XESS_RESULT_WARNING_OLD_DRIVER:
		return "XESS_RESULT_WARNING_OLD_DRIVER";
	case XESS_RESULT_SUCCESS:
		return "XESS_RESULT_SUCCESS";
	case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:
		return "XESS_RESULT_ERROR_UNSUPPORTED_DEVICE";
	case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:
		return "XESS_RESULT_ERROR_UNSUPPORTED_DRIVER";
	case XESS_RESULT_ERROR_UNINITIALIZED:
		return "XESS_RESULT_ERROR_UNINITIALIZED";
	case XESS_RESULT_ERROR_INVALID_ARGUMENT:
		return "XESS_RESULT_ERROR_INVALID_ARGUMENT";
	case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:
		return "XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY";
	case XESS_RESULT_ERROR_DEVICE:
		return "XESS_RESULT_ERROR_DEVICE";
	case XESS_RESULT_ERROR_NOT_IMPLEMENTED:
		return "XESS_RESULT_ERROR_NOT_IMPLEMENTED";
	case XESS_RESULT_ERROR_INVALID_CONTEXT:
		return "XESS_RESULT_ERROR_INVALID_CONTEXT";
	case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS:
		return "XESS_RESULT_ERROR_OPERATION_IN_PROGRESS";
	case XESS_RESULT_ERROR_UNSUPPORTED:
		return "XESS_RESULT_ERROR_UNSUPPORTED";
	case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY:
		return "XESS_RESULT_ERROR_CANT_LOAD_LIBRARY";
	case XESS_RESULT_ERROR_WRONG_CALL_ORDER:
		return "XESS_RESULT_ERROR_WRONG_CALL_ORDER";
	case XESS_RESULT_ERROR_UNKNOWN:
		return "XESS_RESULT_ERROR_UNKNOWN";
	default:
		return "XESS_RESULT_UNRECOGNIZED";
	}
}

const char* IntelXeSS::QualityToString(xess_quality_settings_t a_quality)
{
	switch (a_quality) {
	case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
		return "Ultra Performance";
	case XESS_QUALITY_SETTING_PERFORMANCE:
		return "Performance";
	case XESS_QUALITY_SETTING_BALANCED:
		return "Balanced";
	case XESS_QUALITY_SETTING_QUALITY:
		return "Quality";
	case XESS_QUALITY_SETTING_ULTRA_QUALITY:
		return "Ultra Quality";
	case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
		return "Ultra Quality Plus";
	case XESS_QUALITY_SETTING_AA:
		return "Native Anti-Aliasing";
	default:
		return "Unknown";
	}
}
