#include "Streamline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <limits>
#include <string>
#include <string_view>

#include "../../Deferred.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

namespace
{
	constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;
	constexpr uint32_t kDLSSDiagnosticMaxInitialLogs = 12;
	constexpr uint32_t kDLSSDiagnosticRepeatFrameGap = 300;
	constexpr int32_t kDLSSDiagnosticTextResultCode = std::numeric_limits<int32_t>::min();
	void* s_streamlineDllDirectoryCookie = nullptr;

	enum class D3D11IdleFenceResult : uint8_t
	{
		Ready,
		Pending,
		Failed
	};

	enum class DLSSDiagnosticStage : uint8_t
	{
		ResolveViewport,
		FrameToken,
		SetConstants,
		SetOptions,
		Evaluate,
		Count
	};

	const char* GetDLSSDiagnosticStageName(DLSSDiagnosticStage a_stage)
	{
		switch (a_stage) {
		case DLSSDiagnosticStage::ResolveViewport:
			return "ResolveViewport";
		case DLSSDiagnosticStage::FrameToken:
			return "FrameToken";
		case DLSSDiagnosticStage::SetConstants:
			return "SetConstants";
		case DLSSDiagnosticStage::SetOptions:
			return "SetOptions";
		case DLSSDiagnosticStage::Evaluate:
			return "Evaluate";
		default:
			return "Unknown";
		}
	}

	void ReleaseD3D11IdleFence(ID3D11Query*& a_query)
	{
		if (!a_query)
			return;

		a_query->Release();
		a_query = nullptr;
	}

	bool IsHDRDLSSInputFormat(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return false;
		default:
			return true;
		}
	}

	void EnsureStreamlineDllDirectory(const std::filesystem::path& a_pluginDir)
	{
		if (s_streamlineDllDirectoryCookie)
			return;

		auto kernel32 = GetModuleHandleW(L"kernel32.dll");
		if (!kernel32) {
			logger::warn("[Streamline] Could not get kernel32 module while preparing DLL search path");
			return;
		}

		using AddDllDirectoryFn = void*(WINAPI*)(PCWSTR);
		auto addDllDirectory = reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(kernel32, "AddDllDirectory"));
		if (!addDllDirectory) {
			logger::warn("[Streamline] AddDllDirectory is unavailable; interposer dependency discovery will rely on the DLL load directory and default DLL directories");
			return;
		}

		s_streamlineDllDirectoryCookie = addDllDirectory(a_pluginDir.c_str());
		if (!s_streamlineDllDirectoryCookie) {
			logger::warn(
				"[Streamline] Failed to add Streamline DLL directory {} (error {})",
				stl::utf16_to_utf8(a_pluginDir.wstring()).value_or("<unknown>"),
				GetLastError());
		}
	}

	HMODULE LoadStreamlineDll(const std::filesystem::path& a_path, DWORD& a_error)
	{
		a_error = ERROR_SUCCESS;

		constexpr DWORD kLoadFlags =
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
			LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
			LOAD_LIBRARY_SEARCH_USER_DIRS;

		auto module = LoadLibraryExW(a_path.c_str(), nullptr, kLoadFlags);
		if (module)
			return module;

		a_error = GetLastError();
		logger::warn("[Streamline] LoadLibraryEx failed for {} with error {}",
			stl::utf16_to_utf8(a_path.wstring()).value_or("<unknown>"),
			a_error);
		return nullptr;
	}

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&a_desc);
		return true;
	}

	bool GetDLSSColorBuffersHDR(ID3D11Resource* a_colorIn)
	{
		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(a_colorIn, desc))
			return true;

		return IsHDRDLSSInputFormat(desc.Format);
	}

	std::string FormatExtent(const sl::Extent& a_extent)
	{
		return std::format("top={} left={} width={} height={}", a_extent.top, a_extent.left, a_extent.width, a_extent.height);
	}

	std::string DescribeTextureResource(ID3D11Resource* a_resource)
	{
		if (!a_resource)
			return "null";

		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(a_resource, desc)) {
			return std::format("ptr=0x{:X} non-Texture2D", reinterpret_cast<std::uintptr_t>(a_resource));
		}

		return std::format(
			"ptr=0x{:X} {}x{} fmt={} mips={} array={} samples={} bind=0x{:X} misc=0x{:X} usage={} cpu=0x{:X}",
			reinterpret_cast<std::uintptr_t>(a_resource),
			desc.Width,
			desc.Height,
			magic_enum::enum_name(desc.Format),
			desc.MipLevels,
			desc.ArraySize,
			desc.SampleDesc.Count,
			desc.BindFlags,
			desc.MiscFlags,
			magic_enum::enum_name(desc.Usage),
			desc.CPUAccessFlags);
	}

	bool ShouldLogDLSSDiagnostics()
	{
		// This is why DLSSDiag produced zero lines in every Hot-Envelope capture:
		// it is developer-mode only, and nobody was in developer mode. The extents
		// it reports are the only record of what DLSS was actually told, so let the
		// envelope trace flag enable it too.
		if (globals::features::upscaling.settings.vrHotEnvelope != 0u &&
			globals::features::upscaling.settings.vrHotEnvelopeTrace != 0u) {
			return true;
		}
		return globals::state && globals::state->IsDeveloperMode();
	}

	std::string FormatDLSSDiagnosticResult(int32_t a_resultCode, std::string_view a_resultLabel)
	{
		if (!a_resultLabel.empty())
			return std::string(a_resultLabel);

		return std::format("{}", a_resultCode);
	}

	int32_t QuantizeDLSSDiagnosticFloat(float a_value)
	{
		if (!std::isfinite(a_value))
			return 0;

		const double scaled = static_cast<double>(a_value) * 1000000.0;
		if (scaled > static_cast<double>(std::numeric_limits<int32_t>::max()))
			return std::numeric_limits<int32_t>::max();
		if (scaled < static_cast<double>(std::numeric_limits<int32_t>::min()))
			return std::numeric_limits<int32_t>::min();

		return static_cast<int32_t>(std::lround(scaled));
	}

	bool ShouldEmitDLSSDiagnostic(
		DLSSDiagnosticStage a_stage,
		const Streamline::DLSSDispatchDiagnostics* a_diagnostics,
		int32_t a_resultCode,
		std::string_view a_resultLabel)
	{
		if (!a_diagnostics)
			return false;

		struct ThrottleState
		{
			bool valid = false;
			uint32_t count = 0;
			uint32_t lastFrame = 0;
			uint32_t requestedViewport = 0;
			uint32_t resolvedViewport = 0;
			uint32_t outputWidth = 0;
			uint32_t outputHeight = 0;
			uint32_t qualityMode = 0;
			uint32_t dlssPreset = 0;
			uint32_t viewportRole = 0;
			uint32_t extentInWidth = 0;
			uint32_t extentInHeight = 0;
			uint32_t extentOutWidth = 0;
			uint32_t extentOutHeight = 0;
			int32_t viewportScaleXQ = 0;
			int32_t viewportScaleYQ = 0;
			int32_t pinholeOffsetXQ = 0;
			int32_t pinholeOffsetYQ = 0;
			bool croppedViewport = false;
			int32_t resultCode = 0;
			std::string resultLabel;
			std::string label;
		};

		static std::array<ThrottleState, static_cast<size_t>(DLSSDiagnosticStage::Count) * 2> throttle{};
		const uint32_t boundedEye = globals::game::isVR ? std::min(a_diagnostics->eyeIndex, 1u) : 0u;
		const size_t index = static_cast<size_t>(a_stage) * 2u + boundedEye;
		auto& state = throttle[index];
		const char* label = a_diagnostics->label ? a_diagnostics->label : "DLSS Evaluate";
		const int32_t viewportScaleXQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->viewportScaleX);
		const int32_t viewportScaleYQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->viewportScaleY);
		const int32_t pinholeOffsetXQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->pinholeOffsetX);
		const int32_t pinholeOffsetYQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->pinholeOffsetY);
		const bool signatureChanged =
			!state.valid ||
			state.requestedViewport != static_cast<uint32_t>(a_diagnostics->requestedViewport) ||
			state.resolvedViewport != static_cast<uint32_t>(a_diagnostics->resolvedViewport) ||
			state.outputWidth != a_diagnostics->outputWidth ||
			state.outputHeight != a_diagnostics->outputHeight ||
			state.qualityMode != a_diagnostics->qualityMode ||
			state.dlssPreset != a_diagnostics->dlssPreset ||
			state.viewportRole != static_cast<uint32_t>(a_diagnostics->viewportRole) ||
			state.extentInWidth != a_diagnostics->extentIn.width ||
			state.extentInHeight != a_diagnostics->extentIn.height ||
			state.extentOutWidth != a_diagnostics->extentOut.width ||
			state.extentOutHeight != a_diagnostics->extentOut.height ||
			state.viewportScaleXQ != viewportScaleXQ ||
			state.viewportScaleYQ != viewportScaleYQ ||
			state.pinholeOffsetXQ != pinholeOffsetXQ ||
			state.pinholeOffsetYQ != pinholeOffsetYQ ||
			state.croppedViewport != a_diagnostics->croppedViewport ||
			state.resultCode != a_resultCode ||
			state.resultLabel != a_resultLabel ||
			state.label != label;

		if (signatureChanged) {
			state = {};
			state.valid = true;
			state.requestedViewport = static_cast<uint32_t>(a_diagnostics->requestedViewport);
			state.resolvedViewport = static_cast<uint32_t>(a_diagnostics->resolvedViewport);
			state.outputWidth = a_diagnostics->outputWidth;
			state.outputHeight = a_diagnostics->outputHeight;
			state.qualityMode = a_diagnostics->qualityMode;
			state.dlssPreset = a_diagnostics->dlssPreset;
			state.viewportRole = static_cast<uint32_t>(a_diagnostics->viewportRole);
			state.extentInWidth = a_diagnostics->extentIn.width;
			state.extentInHeight = a_diagnostics->extentIn.height;
			state.extentOutWidth = a_diagnostics->extentOut.width;
			state.extentOutHeight = a_diagnostics->extentOut.height;
			state.viewportScaleXQ = viewportScaleXQ;
			state.viewportScaleYQ = viewportScaleYQ;
			state.pinholeOffsetXQ = pinholeOffsetXQ;
			state.pinholeOffsetYQ = pinholeOffsetYQ;
			state.croppedViewport = a_diagnostics->croppedViewport;
			state.resultCode = a_resultCode;
			state.resultLabel = a_resultLabel;
			state.label = label;
		}

		const uint32_t frame = a_diagnostics->frame;
		const bool emit =
			signatureChanged ||
			state.count < kDLSSDiagnosticMaxInitialLogs ||
			(frame != 0 && state.lastFrame != 0 && frame - state.lastFrame >= kDLSSDiagnosticRepeatFrameGap);

		++state.count;
		if (emit)
			state.lastFrame = frame;

		return emit;
	}

	void LogDLSSDispatchDiagnostics(
		DLSSDiagnosticStage a_stage,
		int32_t a_resultCode,
		std::string_view a_resultLabel,
		const Streamline::DLSSDispatchDiagnostics* a_diagnostics)
	{
		if (!a_diagnostics)
			return;

		if (!ShouldLogDLSSDiagnostics())
			return;

		if (!ShouldEmitDLSSDiagnostic(a_stage, a_diagnostics, a_resultCode, a_resultLabel))
			return;

		const auto& upscaling = globals::features::upscaling;
		const auto& plan = upscaling.GetRuntimeResolutionPlan();
		const char* label = a_diagnostics->label ? a_diagnostics->label : "DLSS Evaluate";
		const auto* frameToken = a_diagnostics->frameToken ? a_diagnostics->frameToken : upscaling.streamline.frameToken;
		const uint32_t frame = a_diagnostics->frame;
		const std::string result = FormatDLSSDiagnosticResult(a_resultCode, a_resultLabel);

		logger::debug(
			"[Streamline][DLSSDiag] stage={} result={} label='{}' frame={} eye={} role={} requestedViewport={} resolvedViewport={} frameToken=0x{:X} quality={} preset={} hdr={} output={}x{} extentIn=[{}] extentOut=[{}] viewportScale={:.6f}x{:.6f} croppedViewport={} pinhole={:.6f},{:.6f} jitter={:.6f},{:.6f} historyReset={} submitStageVR={} presentationActive={} renderScaleActive={} foveatedConfigured={} peripheryTAAConfigured={} optionsCache(valid={} viewport={} output={}x{} quality={} preset={} hdr={} legacy={}) plan(owner={} method={} quality={} display={}x{} render={}x{} final={}x{} foveated={} peripheryTAA={} menu={} knownMenu={} loading={})",
			GetDLSSDiagnosticStageName(a_stage),
			result,
			label,
			frame,
			a_diagnostics->eyeIndex,
			magic_enum::enum_name(a_diagnostics->viewportRole),
			static_cast<uint32_t>(a_diagnostics->requestedViewport),
			static_cast<uint32_t>(a_diagnostics->resolvedViewport),
			reinterpret_cast<std::uintptr_t>(frameToken),
			a_diagnostics->qualityMode,
			a_diagnostics->dlssPreset,
			a_diagnostics->colorBuffersHDR,
			a_diagnostics->outputWidth,
			a_diagnostics->outputHeight,
			FormatExtent(a_diagnostics->extentIn),
			FormatExtent(a_diagnostics->extentOut),
			a_diagnostics->viewportScaleX,
			a_diagnostics->viewportScaleY,
			a_diagnostics->croppedViewport,
			a_diagnostics->pinholeOffsetX,
			a_diagnostics->pinholeOffsetY,
			a_diagnostics->jitterX,
			a_diagnostics->jitterY,
			a_diagnostics->historyResetRequested,
			a_diagnostics->submitStageVRDLSS,
			a_diagnostics->presentationUpscalingActive,
			a_diagnostics->renderScaleActive,
			a_diagnostics->foveatedDispatchEnabled,
			a_diagnostics->peripheryTAAEnabled,
			a_diagnostics->optionsCacheValid,
			a_diagnostics->optionsCacheViewport,
			a_diagnostics->optionsCacheOutputWidth,
			a_diagnostics->optionsCacheOutputHeight,
			a_diagnostics->optionsCacheQualityMode,
			a_diagnostics->optionsCacheDLSSPreset,
			a_diagnostics->optionsCacheHDR,
			a_diagnostics->optionsCacheLegacyProfile,
			magic_enum::enum_name(plan.owner),
			magic_enum::enum_name(plan.upscaleMethod),
			plan.qualityMode,
			static_cast<uint32_t>(plan.trueHMDDisplaySize.x),
			static_cast<uint32_t>(plan.trueHMDDisplaySize.y),
			static_cast<uint32_t>(plan.engineRenderSize.x),
			static_cast<uint32_t>(plan.engineRenderSize.y),
			static_cast<uint32_t>(plan.finalOutputSize.x),
			static_cast<uint32_t>(plan.finalOutputSize.y),
			plan.foveatedActive,
			plan.peripheryTAAActive,
			plan.menuContextActive,
			plan.knownMenuContextActive,
			plan.loadingMenuActive);

		logger::debug(
			"[Streamline][DLSSDiag] resources label='{}' frame={} eye={} colorIn=[{}] colorOut=[{}] depth=[{}] mvec=[{}] reactive=[{}] transparency=[{}]",
			label,
			frame,
			a_diagnostics->eyeIndex,
			DescribeTextureResource(a_diagnostics->colorIn),
			DescribeTextureResource(a_diagnostics->colorOut),
			DescribeTextureResource(a_diagnostics->depth),
			DescribeTextureResource(a_diagnostics->motionVectors),
			DescribeTextureResource(a_diagnostics->reactiveMask),
			DescribeTextureResource(a_diagnostics->transparencyMask));
	}

	void LogDLSSDispatchDiagnostics(DLSSDiagnosticStage a_stage, sl::Result a_result, const Streamline::DLSSDispatchDiagnostics* a_diagnostics)
	{
		const auto resultLabel = magic_enum::enum_name(a_result);
		LogDLSSDispatchDiagnostics(a_stage, static_cast<int32_t>(a_result), resultLabel, a_diagnostics);
	}

	void LogDLSSDispatchDiagnostics(DLSSDiagnosticStage a_stage, const char* a_result, const Streamline::DLSSDispatchDiagnostics* a_diagnostics)
	{
		LogDLSSDispatchDiagnostics(
			a_stage,
			kDLSSDiagnosticTextResultCode,
			a_result ? std::string_view(a_result) : std::string_view(),
			a_diagnostics);
	}

	D3D11IdleFenceResult BeginOrPollD3D11IdleFence(ID3D11DeviceContext* a_context, ID3D11Query*& a_query, const char* a_reason)
	{
		if (!a_context) {
			ReleaseD3D11IdleFence(a_query);
			return D3D11IdleFenceResult::Ready;
		}

		const auto pollFence = [&]() {
			BOOL completed = FALSE;
			const HRESULT dataResult = a_context->GetData(a_query, &completed, sizeof(completed), 0);
			if (dataResult == S_OK && completed) {
				ReleaseD3D11IdleFence(a_query);
				return D3D11IdleFenceResult::Ready;
			}

			if (dataResult == S_FALSE || dataResult == S_OK)
				return D3D11IdleFenceResult::Pending;

			logger::debug("[Streamline] D3D11 idle fence poll failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(dataResult));
			ReleaseD3D11IdleFence(a_query);
			return D3D11IdleFenceResult::Failed;
		};

		if (a_query)
			return pollFence();

		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		if (!device) {
			a_context->Flush();
			return D3D11IdleFenceResult::Ready;
		}

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;

		const HRESULT createResult = device->CreateQuery(&queryDesc, &a_query);
		device->Release();

		if (FAILED(createResult) || !a_query) {
			a_context->Flush();
			logger::debug("[Streamline] D3D11 idle fence creation failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(createResult));
			return D3D11IdleFenceResult::Failed;
		}

		a_context->End(a_query);
		a_context->Flush();
		return pollFence();
	}

}

Streamline::~Streamline()
{
	ResetDLSSIdleFences();
}

void LoggingCallback(sl::LogType type, const char* msg)
{
	// Remove trailing newlines from the raw message
	std::string rawMsg(msg);
	while (!rawMsg.empty() && (rawMsg.back() == '\n' || rawMsg.back() == '\r'))
		rawMsg.pop_back();

	// Remove leading bracketed metadata
	const char* p = msg;
	while (*p == '[') {
		const char* close = strchr(p, ']');
		if (!close)
			break;
		p = close + 1;
		// Skip whitespace after each bracketed section
		while (*p == ' ' || *p == '\t') ++p;
	}
	// Now p points to the first non-bracketed section (file/line info or message)
	std::string cleanMsg(p);
	// Trim leading/trailing whitespace and newlines
	size_t start = cleanMsg.find_first_not_of(" \t\r\n");
	size_t end = cleanMsg.find_last_not_of(" \t\r\n");
	if (start != std::string::npos && end != std::string::npos)
		cleanMsg = cleanMsg.substr(start, end - start + 1);
	else
		cleanMsg.clear();

	// If the cleaned message is empty or only bracketed tokens, log the raw message
	bool onlyBrackets = true;
	for (char c : cleanMsg) {
		if (c != '[' && c != ']' && c != ' ' && c != '\t') {
			onlyBrackets = false;
			break;
		}
	}
	if (cleanMsg.empty() || onlyBrackets) {
		logger::info("[StreamlineSDK:RAW] {}", rawMsg);
		return;
	}

	// Use a clear prefix
	const char* prefix = "[StreamlineSDK]";
	switch (type) {
	case sl::LogType::eInfo:
		logger::info("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eWarn:
		logger::warn("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eError:
		logger::error("{} {}", prefix, cleanMsg);
		break;
	}
}

std::vector<std::pair<std::string, std::string>> Streamline::dllVersions = {};

void Streamline::LoadInterposer()
{
	triedInitialization = true;
	featureCheckComplete = false;

	const std::filesystem::path pluginDir = std::filesystem::path(Streamline::PluginDir);
	std::error_code pluginPathError;
	auto pluginDirAbsolute = std::filesystem::absolute(pluginDir, pluginPathError);
	if (pluginPathError)
		pluginDirAbsolute = pluginDir;
	const std::filesystem::path interposerPath = pluginDirAbsolute / L"sl.interposer.dll";
	EnsureStreamlineDllDirectory(pluginDirAbsolute);
	DWORD errorCode = ERROR_SUCCESS;
	interposer = LoadStreamlineDll(interposerPath, errorCode);
	if (interposer == nullptr) {
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		featureCheckComplete = true;
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	// Dynamically log all DLL versions in the Streamline plugin directory
	Streamline::dllVersions = Util::EnumerateDllVersions(pluginDirAbsolute);
	for (const auto& [name, versionStr] : Streamline::dllVersions)
		logger::info("[Streamline] {} version: {}", name, versionStr);

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	// Set log level from settings
	switch (globals::features::upscaling.settings.streamlineLogLevel) {
	case 2:
		pref.logLevel = sl::LogLevel::eVerbose;
		break;
	case 1:
		pref.logLevel = sl::LogLevel::eDefault;
		break;
	case 0:
	default:
		pref.logLevel = sl::LogLevel::eOff;
		break;
	}
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;
	static std::wstring pluginDirAbsoluteW;
	pluginDirAbsoluteW = pluginDirAbsolute.wstring();
	static const wchar_t* pluginPaths[1]{};
	pluginPaths[0] = pluginDirAbsoluteW.c_str();
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	logger::info("[Streamline] Plugin search path: {}", pluginDirAbsolute.string());

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		featureCheckComplete = true;
	} else {
		initialized = true;
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		InvalidateDLSSOptionsCache();
		reflexOptionsCache = {};
		lastReflexSleepFrame = UINT32_MAX;
		logger::info("[Streamline] Successfully initialized Streamline");
	}
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	featureCheckComplete = false;
	logger::info("[Streamline] Checking features");
	DXGI_ADAPTER_DESC adapterDesc;
	a_adapter->GetDesc(&adapterDesc);
	reflexSupportedOnCurrentAdapter = adapterDesc.VendorId == NVIDIA_VENDOR_ID;

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	auto checkFeatureAvailability = [&](sl::Feature feature, const char* featureName, bool& outAvailable) {
		outAvailable = false;
		bool loaded = false;
		if (SL_FAILED(result, slIsFeatureLoaded(feature, loaded))) {
			logger::warn("[Streamline] {} load-state query failed: {}", featureName, magic_enum::enum_name(result));
			return;
		}
		if (!loaded) {
			logger::info("[Streamline] {} feature is not loaded", featureName);
			sl::FeatureRequirements featureRequirements;
			sl::Result requirementsResult = slGetFeatureRequirements(feature, featureRequirements);
			if (requirementsResult != sl::Result::eOk) {
				logger::info("[Streamline] {} feature failed to load due to: {}", featureName, magic_enum::enum_name(requirementsResult));
			}
			return;
		}

		logger::info("[Streamline] {} feature is loaded", featureName);
		outAvailable = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;
	};

	checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", featureDLSS);
	if (reflexSupportedOnCurrentAdapter) {
		checkFeatureAvailability(sl::kFeatureReflex, "Reflex", featureReflex);
		checkFeatureAvailability(sl::kFeaturePCL, "PCL", featurePCL);
	} else {
		featureReflex = false;
		featurePCL = false;
	}

	if (featureDLSS) {
		isRTXBelow40series = IsRTXAndBelow40Series(a_adapter);

		if (isRTXBelow40series)
			logger::info("[Streamline] Older RTX GPU detected, DLSS 4.0 will be used instead of DLSS 4.5");
		else
			logger::info("[Streamline] Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
	if (reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Reflex {} available", featureReflex ? "is" : "is not");
		logger::info("[Streamline] PCL {} available", featurePCL ? "is" : "is not");
	} else {
		logger::info("[Streamline] Reflex/PCL disabled on non-NVIDIA adapter");
	}
	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
	featureCheckComplete = true;
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	slReflexGetState = nullptr;
	slReflexSleep = nullptr;
	slReflexSetOptions = nullptr;
	slPCLSetMarker = nullptr;
	featureReflex = false;
	featurePCL = false;

	if (slGetFeatureFunction && reflexSupportedOnCurrentAdapter) {
		if (slSetFeatureLoaded) {
			const auto requestFeatureLoad = [&](sl::Feature feature, const char* featureName) {
				const sl::Result loadResult = slSetFeatureLoaded(feature, true);
				if (loadResult != sl::Result::eOk)
					logger::warn("[Streamline] Failed to request {} load: {}", featureName, magic_enum::enum_name(loadResult));
			};

			requestFeatureLoad(sl::kFeatureReflex, "Reflex");
			requestFeatureLoad(sl::kFeaturePCL, "PCL");
		}

		const auto bindFeatureFn = [&](sl::Feature feature, const char* functionName, void*& fn) {
			fn = nullptr;
			const sl::Result bindResult = slGetFeatureFunction(feature, functionName, fn);
			if (bindResult != sl::Result::eOk)
				logger::warn("[Streamline] {} bind failed with {}", functionName, magic_enum::enum_name(bindResult));
			return bindResult == sl::Result::eOk && fn != nullptr;
		};

		bool reflexFnsBound = true;
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
		featureReflex = reflexFnsBound && slReflexSetOptions && slReflexSleep;

		if (!featureReflex) {
			logger::warn("[Streamline] Reflex functions are missing; Reflex runtime controls will be disabled");
		} else {
			logger::info("[Streamline] Reflex runtime controls are available");
		}

		slPCLSetMarker = nullptr;
		bool pclFnBound = bindFeatureFn(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker);
		featurePCL = pclFnBound && slPCLSetMarker;
		if (!featurePCL) {
			logger::warn("[Streamline] PCL marker function is unavailable; marker optimization requests will be ignored");
		} else {
			logger::info("[Streamline] PCL marker interface is available");
		}
	} else if (!reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Skipping Reflex/PCL binding on non-NVIDIA adapter");
	}

	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
bool Streamline::EnsureFrameToken()
{
	if (!initialized || !slGetNewFrameToken || !globals::state)
		return false;

	if (!frameChecker.IsNewFrame())
		return frameToken != nullptr;

	if (SL_FAILED(result, slGetNewFrameToken(frameToken, &globals::state->frameCount))) {
		logger::error("[Streamline] Could not get frame token: {}", magic_enum::enum_name(result));
		frameToken = nullptr;
		return false;
	}

	return frameToken != nullptr;
}

bool Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport, uint32_t eyeIndex, float viewportScaleX, float viewportScaleY, float pinholeOffsetX, float pinholeOffsetY, const DLSSDispatchDiagnostics* diagnostics)
{
	if (!globals::features::upscaling.streamline.initialized)
		return false;

	if (!EnsureFrameToken()) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::FrameToken, "unavailable", diagnostics);
		return false;
	}

	// In VR, we need to set constants for each viewport/eye separately
	// In non-VR, this is called once per frame
	auto state = globals::state;
	auto& upscaling = globals::features::upscaling;
	if (!state)
		return false;
	bool applyCroppedConstantsCorrection = false;
	float clampedViewportScaleX = std::clamp(viewportScaleX, 1e-4f, 1.0f);
	float clampedViewportScaleY = std::clamp(viewportScaleY, 1e-4f, 1.0f);
	float clampedPinholeOffsetX = std::isfinite(pinholeOffsetX) ? std::clamp(pinholeOffsetX, -1.0f, 1.0f) : 0.0f;
	float clampedPinholeOffsetY = std::isfinite(pinholeOffsetY) ? std::clamp(pinholeOffsetY, -1.0f, 1.0f) : 0.0f;
	if (!globals::game::isVR) {
		clampedViewportScaleX = 1.0f;
		clampedViewportScaleY = 1.0f;
		clampedPinholeOffsetX = 0.0f;
		clampedPinholeOffsetY = 0.0f;
	}

	sl::Constants slConstants = {};

	// Calculate aspect ratio for the SINGLE EYE
	float2 fullOutputSize = upscaling.GetRuntimeResolutionPlan().finalOutputSize;
	if (fullOutputSize.x <= 0.0f || fullOutputSize.y <= 0.0f)
		fullOutputSize = state->screenSize;
	float eyeWidth = fullOutputSize.x * (globals::game::isVR ? 0.5f : 1.0f);
	float eyeHeight = fullOutputSize.y;
	slConstants.cameraAspectRatio = (eyeWidth * clampedViewportScaleX) / (eyeHeight * clampedViewportScaleY);

	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraNear = *globals::game::cameraNear;
	slConstants.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse(eyeIndex).Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered(eyeIndex).Transpose();

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	if (globals::game::isVR) {
		const bool isCroppedViewport = clampedViewportScaleX < 0.999f || clampedViewportScaleY < 0.999f;
		applyCroppedConstantsCorrection = isCroppedViewport;
		if (applyCroppedConstantsCorrection) {
			const float invScaleX = 1.0f / clampedViewportScaleX;
			const float invScaleY = 1.0f / clampedViewportScaleY;

			// Match projection to the cropped DLSS viewport so temporal reprojection
			// operates in the same clip space as color/depth/mvec inputs.
			slConstants.cameraViewToClip[0].x *= invScaleX;
			slConstants.cameraViewToClip[0].y *= invScaleX;
			slConstants.cameraViewToClip[0].z *= invScaleX;
			slConstants.cameraViewToClip[0].w *= invScaleX;
			slConstants.cameraViewToClip[1].x *= invScaleY;
			slConstants.cameraViewToClip[1].y *= invScaleY;
			slConstants.cameraViewToClip[1].z *= invScaleY;
			slConstants.cameraViewToClip[1].w *= invScaleY;

			// cameraFOV is vertical; scale by cropped Y region.
			slConstants.cameraFOV = 2.0f * atanf(clampedViewportScaleY * tanf(slConstants.cameraFOV * 0.5f));
			slConstants.cameraPinholeOffset = {
				clampedPinholeOffsetX / clampedViewportScaleX,
				clampedPinholeOffsetY / clampedViewportScaleY
			};
		}

		// VR: compute clipToCameraView / clipToPrevClip / prevClipToClip from Skyrim's per-eye matrices.
		// recalculateCameraMatrices() uses a single static prev-frame slot -- unusable for two viewports.
		sl::matrixFullInvert(slConstants.clipToCameraView, slConstants.cameraViewToClip);

		auto currViewProj = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex).Transpose();
		auto prevViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered(eyeIndex).Transpose();

		sl::float4x4 currViewProjSL = *(sl::float4x4*)&currViewProj;
		sl::float4x4 prevViewProjSL = *(sl::float4x4*)&prevViewProj;

		sl::float4x4 invCurrViewProj;
		sl::matrixFullInvert(invCurrViewProj, currViewProjSL);
		sl::matrixMul(slConstants.clipToPrevClip, invCurrViewProj, prevViewProjSL);

		if (applyCroppedConstantsCorrection) {
			const float invScaleX = 1.0f / clampedViewportScaleX;
			const float invScaleY = 1.0f / clampedViewportScaleY;
			const float leftFactors[4] = { clampedViewportScaleX, clampedViewportScaleY, 1.0f, 1.0f };
			const float rightFactors[4] = { invScaleX, invScaleY, 1.0f, 1.0f };

			// Conjugate clipToPrevClip into cropped clip-space basis:
			// CTP_cropped = inv(S) * CTP * S
			float* ctpValues = &slConstants.clipToPrevClip[0].x;
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t col = 0; col < 4; ++col) {
					ctpValues[row * 4 + col] *= leftFactors[row] * rightFactors[col];
				}
			}
		}

		sl::matrixFullInvert(slConstants.prevClipToClip, slConstants.clipToPrevClip);
	} else {
		recalculateCameraMatrices(slConstants);
	}

	auto jitter = upscaling.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	const bool requestHistoryReset = upscaling.ShouldResetHistoryThisFrame();
	slConstants.reset = requestHistoryReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	if (globals::game::isVR && applyCroppedConstantsCorrection) {
		slConstants.mvecScale = { 1.0f / clampedViewportScaleX, 1.0f / clampedViewportScaleY };
	} else {
		slConstants.mvecScale = { 1.0f, 1.0f };
	}
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	const auto makeFrameConstantsSignature = [&]() {
		DLSSFrameConstantsCache signature{};
		signature.valid = true;
		signature.frame = diagnostics ? diagnostics->frame : state->frameCount;
		signature.frameToken = reinterpret_cast<std::uintptr_t>(frameToken);
		signature.viewport = static_cast<uint32_t>(p_viewport);
		signature.eyeIndex = eyeIndex;
		signature.viewportRole = diagnostics ? static_cast<uint32_t>(diagnostics->viewportRole) : static_cast<uint32_t>(DLSSViewportRole::FullEye);
		signature.outputWidth = diagnostics ? diagnostics->outputWidth : 0u;
		signature.outputHeight = diagnostics ? diagnostics->outputHeight : 0u;
		signature.qualityMode = diagnostics ? diagnostics->qualityMode : 0u;
		signature.dlssPreset = diagnostics ? diagnostics->dlssPreset : 0u;
		signature.extentInWidth = diagnostics ? diagnostics->extentIn.width : 0u;
		signature.extentInHeight = diagnostics ? diagnostics->extentIn.height : 0u;
		signature.extentOutWidth = diagnostics ? diagnostics->extentOut.width : 0u;
		signature.extentOutHeight = diagnostics ? diagnostics->extentOut.height : 0u;
		signature.viewportScaleXQ = QuantizeDLSSDiagnosticFloat(clampedViewportScaleX);
		signature.viewportScaleYQ = QuantizeDLSSDiagnosticFloat(clampedViewportScaleY);
		signature.pinholeOffsetXQ = QuantizeDLSSDiagnosticFloat(clampedPinholeOffsetX);
		signature.pinholeOffsetYQ = QuantizeDLSSDiagnosticFloat(clampedPinholeOffsetY);
		signature.jitterXQ = QuantizeDLSSDiagnosticFloat(upscaling.jitter.x);
		signature.jitterYQ = QuantizeDLSSDiagnosticFloat(upscaling.jitter.y);
		signature.historyResetRequested = requestHistoryReset;
		return signature;
	};
	const auto frameConstantsMatch = [](const DLSSFrameConstantsCache& a_cached, const DLSSFrameConstantsCache& a_signature) {
		return a_cached.valid &&
		       a_cached.frame == a_signature.frame &&
		       a_cached.frameToken == a_signature.frameToken &&
		       a_cached.viewport == a_signature.viewport &&
		       a_cached.eyeIndex == a_signature.eyeIndex &&
		       a_cached.viewportRole == a_signature.viewportRole &&
		       a_cached.outputWidth == a_signature.outputWidth &&
		       a_cached.outputHeight == a_signature.outputHeight &&
		       a_cached.qualityMode == a_signature.qualityMode &&
		       a_cached.dlssPreset == a_signature.dlssPreset &&
		       a_cached.extentInWidth == a_signature.extentInWidth &&
		       a_cached.extentInHeight == a_signature.extentInHeight &&
		       a_cached.extentOutWidth == a_signature.extentOutWidth &&
		       a_cached.extentOutHeight == a_signature.extentOutHeight &&
		       a_cached.viewportScaleXQ == a_signature.viewportScaleXQ &&
		       a_cached.viewportScaleYQ == a_signature.viewportScaleYQ &&
		       a_cached.pinholeOffsetXQ == a_signature.pinholeOffsetXQ &&
		       a_cached.pinholeOffsetYQ == a_signature.pinholeOffsetYQ &&
		       a_cached.jitterXQ == a_signature.jitterXQ &&
		       a_cached.jitterYQ == a_signature.jitterYQ &&
		       a_cached.historyResetRequested == a_signature.historyResetRequested;
	};
	const bool canAcceptDuplicateConstants =
		diagnostics &&
		diagnostics->submitStageVRDLSS &&
		(diagnostics->viewportRole == DLSSViewportRole::FullEye ||
			diagnostics->viewportRole == DLSSViewportRole::SubmitStageFoveatedCenter);
	DLSSFrameConstantsCache frameConstantsSignature{};
	if (canAcceptDuplicateConstants)
		frameConstantsSignature = makeFrameConstantsSignature();
	const auto hasCachedFrameConstantsSignature = [&]() {
		if (!canAcceptDuplicateConstants)
			return false;

		for (const auto& cachedSignature : dlssFrameConstantsCache) {
			if (frameConstantsMatch(cachedSignature, frameConstantsSignature))
				return true;
		}
		return false;
	};
	if (hasCachedFrameConstantsSignature()) {
		lastDLSSFailureDuplicatedConstants = false;
		return true;
	}

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
		const bool duplicatedConstants = res == sl::Result::eErrorDuplicatedConstants;
		lastDLSSFailureDuplicatedConstants = duplicatedConstants;
		const auto resultLabel = magic_enum::enum_name(res);
		if (diagnostics) {
			if (ShouldEmitDLSSDiagnostic(DLSSDiagnosticStage::SetConstants, diagnostics, static_cast<int32_t>(res), resultLabel)) {
				logger::error(
					"[Streamline] Could not set constants for eye {}: result={} label='{}' role={} viewport={} frame={} extentIn={}x{} extentOut={}x{} output={}x{} scale={:.6f}x{:.6f} pinhole={:.6f},{:.6f} duplicateConstants={}",
					eyeIndex,
					FormatDLSSDiagnosticResult(static_cast<int32_t>(res), resultLabel),
					diagnostics->label ? diagnostics->label : "DLSS Evaluate",
					magic_enum::enum_name(diagnostics->viewportRole),
					static_cast<uint32_t>(p_viewport),
					diagnostics->frame,
					diagnostics->extentIn.width,
					diagnostics->extentIn.height,
					diagnostics->extentOut.width,
					diagnostics->extentOut.height,
					diagnostics->outputWidth,
					diagnostics->outputHeight,
					diagnostics->viewportScaleX,
					diagnostics->viewportScaleY,
					diagnostics->pinholeOffsetX,
					diagnostics->pinholeOffsetY,
					lastDLSSFailureDuplicatedConstants);
			}
		} else {
			logger::error("[Streamline] Could not set constants for eye {}", eyeIndex);
		}
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::SetConstants, res, diagnostics);
		return false;
	}

	if (canAcceptDuplicateConstants) {
		auto* targetSlot = &dlssFrameConstantsCache[static_cast<uint32_t>(p_viewport) % dlssFrameConstantsCache.size()];
		for (auto& cachedSignature : dlssFrameConstantsCache) {
			if (!cachedSignature.valid ||
				(cachedSignature.viewport == frameConstantsSignature.viewport &&
					cachedSignature.eyeIndex == frameConstantsSignature.eyeIndex &&
					cachedSignature.viewportRole == frameConstantsSignature.viewportRole)) {
				targetSlot = &cachedSignature;
				break;
			}
		}
		*targetSlot = frameConstantsSignature;
	}
	return true;
}

bool Streamline::IsRTXAndBelow40Series(IDXGIAdapter* a_adapter)
{
	DXGI_ADAPTER_DESC adapterDesc = {};

	a_adapter->GetDesc(&adapterDesc);

	UINT vendorId = adapterDesc.VendorId;
	UINT deviceId = adapterDesc.DeviceId;

	// Check if NVIDIA
	if (vendorId != 0x10DE)
		return false;

	// RTX 30 series (Ampere) - 0x2200-0x25FF
	if (deviceId >= 0x2200 && deviceId <= 0x2600)
		return true;

	// RTX 20 series (Turing with RT cores) - 0x1E00-0x1FFF
	if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
		return true;

	return false;
}

bool Streamline::SetDLSSOptions(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t width, uint32_t height, bool colorBuffersHDR, uint32_t qualityMode, uint32_t dlssPreset, const DLSSDispatchDiagnostics* diagnostics)
{
	if (!slDLSSSetOptions)
		return false;

	// Map custom render-scale presets to the nearest supported DLSS mode.
	qualityMode = std::min(qualityMode, Upscaling::kQualityModeMaxIndex);
	dlssPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);

	bool useLegacyProfile = isRTXBelow40series;
	auto& cache = GetDLSSOptionsCache(viewportRole, eyeIndex, qualityMode, dlssPreset);
	const uint32_t viewportKey = static_cast<uint32_t>(p_viewport);
	if (cache.valid &&
		cache.viewport == viewportKey &&
		cache.outputWidth == width &&
		cache.outputHeight == height &&
		cache.qualityMode == qualityMode &&
		cache.dlssPreset == dlssPreset &&
		cache.isHDR == colorBuffersHDR &&
		cache.useLegacyProfile == useLegacyProfile) {
		return true;
	}

	sl::DLSSOptions dlssOptions{};
	switch (qualityMode) {
	case 1:
	case 2:
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 5:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 6:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = height;
	dlssOptions.colorBuffersHDR = colorBuffersHDR ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	sl::DLSSPreset selectedPreset = sl::DLSSPreset::ePresetK;
	switch (dlssPreset) {
	case Upscaling::kDLSSPresetJ:
		selectedPreset = sl::DLSSPreset::ePresetJ;
		break;
	case Upscaling::kDLSSPresetK:
		selectedPreset = sl::DLSSPreset::ePresetK;
		break;
	case Upscaling::kDLSSPresetL:
		selectedPreset = sl::DLSSPreset::ePresetL;
		break;
	case Upscaling::kDLSSPresetM:
		selectedPreset = sl::DLSSPreset::ePresetM;
		break;
	case Upscaling::kDLSSPresetF:
		selectedPreset = sl::DLSSPreset::ePresetF;
		break;
	case Upscaling::kDLSSPresetE:
		selectedPreset = sl::DLSSPreset::ePresetE;
		break;
	default:
		selectedPreset = sl::DLSSPreset::ePresetK;
		break;
	}

	dlssOptions.dlaaPreset = selectedPreset;
	dlssOptions.ultraQualityPreset = selectedPreset;
	dlssOptions.qualityPreset = selectedPreset;
	dlssOptions.balancedPreset = selectedPreset;
	dlssOptions.performancePreset = selectedPreset;
	dlssOptions.ultraPerformancePreset = selectedPreset;

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	// Hot-Envelope groundwork: what input range will ONE DLSS context accept?
	//
	// CSX keeps a context per qualityMode + dlssPreset, each created at its own
	// input resolution, and recreating them is what drags in the vendor-resource
	// reset. If [renderWidthMin, renderWidthMax] for a single mode already spans
	// the envelope, one context can serve every quality at or below the boot
	// quality by varying the tagged input extent, and the per-quality contexts
	// can go away entirely rather than being reset more cleverly.
	//
	// Diagnostic only - nothing below reads it. This function early-returns on a
	// cache hit, so it runs on change rather than per frame.
	if (slDLSSGetOptimalSettings) {
		sl::DLSSOptimalSettings optimal{};
		if (slDLSSGetOptimalSettings(dlssOptions, optimal) == sl::Result::eOk) {
			logger::info(
				"[Streamline][HotEnvelope] DLSS mode {} output {}x{}: optimal {}x{}, accepts {}x{} .. {}x{}",
				magic_enum::enum_name(dlssOptions.mode),
				width,
				height,
				optimal.optimalRenderWidth,
				optimal.optimalRenderHeight,
				optimal.renderWidthMin,
				optimal.renderHeightMin,
				optimal.renderWidthMax,
				optimal.renderHeightMax);
		}
	}

	if (SL_FAILED(result, slDLSSSetOptions(p_viewport, dlssOptions))) {
		logger::critical("[Streamline] Could not enable DLSS for viewport {} eye {}: {}",
			static_cast<uint32_t>(p_viewport),
			eyeIndex,
			magic_enum::enum_name(result));
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::SetOptions, result, diagnostics);
		cache.valid = false;
		return false;
	}

	cache.valid = true;
	cache.viewport = viewportKey;
	cache.outputWidth = width;
	cache.outputHeight = height;
	cache.qualityMode = qualityMode;
	cache.dlssPreset = dlssPreset;
	cache.isHDR = colorBuffersHDR;
	cache.useLegacyProfile = useLegacyProfile;
	if (p_viewport == viewport) {
		activeDLSSViewportResourcesAllocated[0] = true;
	} else if (p_viewport == viewportRight) {
		activeDLSSViewportResourcesAllocated[1] = true;
	} else {
		for (auto& roleSlots : vrDLSSViewportSlots) {
			for (auto& slot : roleSlots) {
				for (uint32_t eye = 0; eye < 2; ++eye) {
					if (slot.viewport[eye] == p_viewport) {
						slot.resourcesAllocated[eye] = true;
						return true;
					}
				}
			}
		}
	}
	return true;
}

int Streamline::FindVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset) const
{
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	for (uint32_t slot = 0; slot < kVRDLSSViewportSlotCount; ++slot) {
		const auto& viewportSlot = vrDLSSViewportSlots[roleIndex][slot];
		if (viewportSlot.valid &&
			viewportSlot.qualityMode == clampedQualityMode &&
			viewportSlot.dlssPreset == clampedPreset) {
			return static_cast<int>(slot);
		}
	}

	return -1;
}

bool Streamline::TryResolveExistingVRDLSSViewport(
	DLSSViewportRole a_viewportRole,
	uint32_t a_eyeIndex,
	uint32_t a_qualityMode,
	uint32_t a_dlssPreset,
	uint32_t a_outputWidth,
	uint32_t a_outputHeight,
	ID3D11Resource* a_colorInput,
	sl::ViewportHandle& a_viewport) const
{
	if (!globals::game::isVR || a_eyeIndex >= 2 ||
		!initialized || !featureDLSS || !slEvaluateFeature || !slDLSSSetOptions ||
		!globals::d3d::context || !a_colorInput || !a_outputWidth || !a_outputHeight) {
		return false;
	}
	if (static_cast<uint32_t>(a_viewportRole) >= kVRDLSSViewportRoleCount)
		return false;

	const uint32_t roleIndex = GetDLSSViewportRoleIndex(a_viewportRole);
	if (pendingDLSSResourceFreeIdleFence ||
		pendingVRDLSSSlotRecycleIdleFences[roleIndex]) {
		return false;
	}

	const uint32_t qualityMode = std::min<uint32_t>(
		a_qualityMode,
		Upscaling::kQualityModeMaxIndex);
	const uint32_t dlssPreset = Upscaling::ClampDLSSPresetUInt(a_dlssPreset);
	const int slotIndex = FindVRDLSSViewportSlot(
		a_viewportRole,
		qualityMode,
		dlssPreset);
	if (slotIndex < 0)
		return false;

	const auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	const auto& cache = slot.optionsCache[a_eyeIndex];
	const auto resolvedViewport = slot.viewport[a_eyeIndex];
	const bool colorBuffersHDR = GetDLSSColorBuffersHDR(a_colorInput);
	if (!slot.resourcesAllocated[a_eyeIndex] ||
		!cache.valid ||
		cache.viewport != static_cast<uint32_t>(resolvedViewport) ||
		cache.outputWidth != a_outputWidth ||
		cache.outputHeight != a_outputHeight ||
		cache.qualityMode != qualityMode ||
		cache.dlssPreset != dlssPreset ||
		cache.isHDR != colorBuffersHDR ||
		cache.useLegacyProfile != isRTXBelow40series) {
		return false;
	}

	a_viewport = resolvedViewport;
	return true;
}

int Streamline::ChooseVRDLSSViewportSlotForAllocation(DLSSViewportRole viewportRole) const
{
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	for (uint32_t slot = 0; slot < kVRDLSSViewportSlotCount; ++slot) {
		if (!vrDLSSViewportSlots[roleIndex][slot].valid)
			return static_cast<int>(slot);
	}

	uint32_t lruSlot = 0;
	uint64_t lruCounter = vrDLSSViewportSlots[roleIndex][0].lastUse;
	for (uint32_t slot = 1; slot < kVRDLSSViewportSlotCount; ++slot) {
		if (vrDLSSViewportSlots[roleIndex][slot].lastUse < lruCounter) {
			lruCounter = vrDLSSViewportSlots[roleIndex][slot].lastUse;
			lruSlot = slot;
		}
	}

	return static_cast<int>(lruSlot);
}

bool Streamline::FreeDLSSViewportResources(sl::ViewportHandle a_viewport, uint32_t a_eyeIndex, bool a_logFailures)
{
	if (!slDLSSSetOptions || !slFreeResources)
		return true;

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;

	const sl::Result optionsResult = slDLSSSetOptions(a_viewport, dlssOptions);
	if (a_logFailures && optionsResult != sl::Result::eOk) {
		logger::debug("[Streamline] DLSS off failed for viewport {} eye {}: {}",
			static_cast<uint32_t>(a_viewport),
			a_eyeIndex,
			magic_enum::enum_name(optionsResult));
	}

	const sl::Result freeResult = slFreeResources(sl::kFeatureDLSS, a_viewport);
	if (a_logFailures && freeResult != sl::Result::eOk) {
		logger::debug("[Streamline] DLSS resource free failed for viewport {} eye {}: {}",
			static_cast<uint32_t>(a_viewport),
			a_eyeIndex,
			magic_enum::enum_name(freeResult));
	}
	return freeResult == sl::Result::eOk;
}

bool Streamline::FreeVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t slotIndex, bool logFailures)
{
	if (slotIndex >= kVRDLSSViewportSlotCount)
		return true;

	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	if (!slot.valid)
		return true;

	bool slotResourcesFreed = true;
	for (uint32_t eye = 0; eye < 2; ++eye) {
		slot.resourcesAllocated[eye] = slot.resourcesAllocated[eye] || slot.optionsCache[eye].valid;
		const bool shouldLogFailures = logFailures || slot.optionsCache[eye].valid;
		if (slot.resourcesAllocated[eye]) {
			const bool eyeFreed = FreeDLSSViewportResources(slot.viewport[eye], eye, shouldLogFailures);
			slotResourcesFreed = eyeFreed && slotResourcesFreed;
			if (eyeFreed)
				slot.resourcesAllocated[eye] = false;
		}
		slot.optionsCache[eye] = {};
	}

	if (slot.resourcesAllocated[0] || slot.resourcesAllocated[1])
		return false;

	slot.valid = false;
	slot.qualityMode = 0;
	slot.dlssPreset = 0;
	slot.lastUse = 0;
	return slotResourcesFreed;
}

Streamline::DLSSViewportPreparationResult Streamline::PrepareVRDLSSViewport(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset)
{
	if (!globals::game::isVR)
		return DLSSViewportPreparationResult::Ready;

	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	// Full-eye and foveated-center caches advance independently. A cache hit
	// in one role must never consume the fence that protects another role's
	// LRU victim, or that other role will restart its drain indefinitely.
	auto& pendingSlotRecycleIdleFence = pendingVRDLSSSlotRecycleIdleFences[roleIndex];

	int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex >= 0) {
		// A latest-wins request can supersede a pending miss with a cache hit.
		// Drain that abandoned fence without delaying the already-resident target.
		if (pendingSlotRecycleIdleFence) {
			if (auto context = globals::d3d::context) {
				const auto idleFenceResult = BeginOrPollD3D11IdleFence(
					context,
					pendingSlotRecycleIdleFence,
					"superseded VR DLSS viewport slot recycle");
				if (idleFenceResult == D3D11IdleFenceResult::Failed)
					return DLSSViewportPreparationResult::Failed;
			} else {
				ReleaseD3D11IdleFence(pendingSlotRecycleIdleFence);
			}
		}
		return DLSSViewportPreparationResult::Ready;
	}

	slotIndex = ChooseVRDLSSViewportSlotForAllocation(viewportRole);
	if (slotIndex < 0)
		slotIndex = 0;

	auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	if (slot.valid) {
		if (auto context = globals::d3d::context) {
			const auto idleFenceResult = BeginOrPollD3D11IdleFence(context, pendingSlotRecycleIdleFence, "VR DLSS viewport slot recycle");
			if (idleFenceResult == D3D11IdleFenceResult::Pending) {
				static bool loggedSlotRecyclePending = false;
				if (!loggedSlotRecyclePending) {
					logger::warn("[Streamline] Deferring VR DLSS viewport preparation because the previous slot is still in flight.");
					loggedSlotRecyclePending = true;
				}
				nonVRDLSSOptionsCache.valid = false;
				return DLSSViewportPreparationResult::Pending;
			}
			if (idleFenceResult == D3D11IdleFenceResult::Failed) {
				static bool loggedSlotRecycleFenceFailure = false;
				if (!loggedSlotRecycleFenceFailure) {
					logger::warn("[Streamline] VR DLSS viewport preparation failed because the slot recycle fence could not be queried.");
					loggedSlotRecycleFenceFailure = true;
				}
				nonVRDLSSOptionsCache.valid = false;
				return DLSSViewportPreparationResult::Failed;
			}
		} else {
			ReleaseD3D11IdleFence(pendingSlotRecycleIdleFence);
		}
		if (!FreeVRDLSSViewportSlot(viewportRole, static_cast<uint32_t>(slotIndex), true)) {
			static bool loggedSlotRecycleFreeFailure = false;
			if (!loggedSlotRecycleFreeFailure) {
				logger::warn("[Streamline] VR DLSS viewport preparation failed because the previous slot resources could not be released.");
				loggedSlotRecycleFreeFailure = true;
			}
			nonVRDLSSOptionsCache.valid = false;
			return DLSSViewportPreparationResult::Failed;
		}
	}

	slot.valid = true;
	slot.qualityMode = clampedQualityMode;
	slot.dlssPreset = clampedPreset;
	slot.lastUse = 0;
	slot.resourcesAllocated[0] = false;
	slot.resourcesAllocated[1] = false;
	for (auto& optionsCache : slot.optionsCache)
		optionsCache = {};

	const uint32_t viewportBase =
		kVRDLSSSlotViewportBase +
		(roleIndex * kVRDLSSSlotViewportRoleStride) +
		(static_cast<uint32_t>(slotIndex) * kVRDLSSSlotViewportEyeStride);
	slot.viewport[0] = sl::ViewportHandle(viewportBase);
	slot.viewport[1] = sl::ViewportHandle(viewportBase + 1);
	return DLSSViewportPreparationResult::Ready;
}

bool Streamline::ResolveDLSSViewport(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset, sl::ViewportHandle& outViewport)
{
	outViewport = p_viewport;
	if (!globals::game::isVR)
		return true;

	if (PrepareVRDLSSViewport(viewportRole, qualityMode, dlssPreset) != DLSSViewportPreparationResult::Ready)
		return false;

	const uint32_t eye = eyeIndex > 0 ? 1u : 0u;
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	const int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex < 0) {
		nonVRDLSSOptionsCache.valid = false;
		return false;
	}

	auto& activeSlot = vrDLSSViewportSlots[roleIndex][slotIndex];
	activeSlot.lastUse = ++vrDLSSViewportUseCounter;
	outViewport = activeSlot.viewport[eye];
	return true;
}

Streamline::DLSSOptionsCache& Streamline::GetDLSSOptionsCache(DLSSViewportRole viewportRole, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset)
{
	if (!globals::game::isVR)
		return nonVRDLSSOptionsCache;

	const uint32_t eye = eyeIndex > 0 ? 1u : 0u;
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);

	const int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex >= 0)
		return vrDLSSViewportSlots[roleIndex][slotIndex].optionsCache[eye];

	// Fallback for unexpected ordering; keeps behavior deterministic and forces option re-apply.
	nonVRDLSSOptionsCache.valid = false;
	return nonVRDLSSOptionsCache;
}

void Streamline::InvalidateDLSSOptionsCache()
{
	nonVRDLSSOptionsCache = {};
	dlssFrameConstantsCache = {};
	for (auto& roleSlots : vrDLSSViewportSlots) {
		for (auto& slot : roleSlots) {
			for (auto& optionsCache : slot.optionsCache)
				optionsCache = {};
		}
	}
}

void Streamline::ResetDLSSIdleFences()
{
	ReleaseD3D11IdleFence(pendingDLSSResourceFreeIdleFence);
	for (auto& pendingSlotRecycleIdleFence : pendingVRDLSSSlotRecycleIdleFences)
		ReleaseD3D11IdleFence(pendingSlotRecycleIdleFence);
}

void Streamline::ResetFrameTracking()
{
	frameToken = nullptr;
	frameChecker = {};
	dlssFrameConstantsCache = {};
}

bool Streamline::HasDLSSResourcesPendingTeardown() const
{
	if (pendingDLSSResourceFreeIdleFence ||
		std::ranges::any_of(pendingVRDLSSSlotRecycleIdleFences, [](const auto* a_fence) { return a_fence != nullptr; })) {
		return true;
	}

	// If DLSS is not active/available in this process, cached slot metadata
	// should not trigger a teardown cooldown by itself.
	if (!initialized || !featureDLSS)
		return false;

	if (activeDLSSViewportResourcesAllocated[0] || activeDLSSViewportResourcesAllocated[1])
		return true;

	if (nonVRDLSSOptionsCache.valid)
		return true;

	for (const auto& roleSlots : vrDLSSViewportSlots) {
		for (const auto& slot : roleSlots) {
			if (slot.valid)
				return true;

			if (slot.resourcesAllocated[0] || slot.resourcesAllocated[1])
				return true;

			for (const auto& optionsCache : slot.optionsCache) {
				if (optionsCache.valid)
					return true;
			}
		}
	}

	return false;
}

bool Streamline::EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth,
	float pinholeOffsetX, float pinholeOffsetY, const char* label, DLSSViewportRole viewportRole,
	bool useAuthoritativeProfile, uint32_t authoritativeQualityMode, uint32_t authoritativeDLSSPreset)
{
	auto context = globals::d3d::context;
	if (!initialized || !featureDLSS || !slEvaluateFeature || !context ||
		!colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask)
		return false;
	if (globals::game::isVR && eyeIndex > 1)
		return false;

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	auto& upscaling = globals::features::upscaling;
	auto state = globals::state;
	const bool vendorLifecycleMutationDeferred =
		globals::game::isVR &&
		upscaling.ShouldDeferVRVendorLifecycleMutation();
	const bool existingProviderOnly =
		vendorLifecycleMutationDeferred || useAuthoritativeProfile;
	const auto existingProvider =
		vendorLifecycleMutationDeferred && !useAuthoritativeProfile ?
			upscaling.GetExistingVRVendorProviderSnapshot() :
			Upscaling::VRExistingVendorProviderSnapshot{};
	float viewportScaleX = 1.0f;
	float viewportScaleY = 1.0f;
	if (state) {
		const auto& resolutionPlan = upscaling.GetRuntimeResolutionPlan();
		auto fullOutputSize = resolutionPlan.finalOutputSize;
		if (fullOutputSize.x <= 0.0f || fullOutputSize.y <= 0.0f)
			fullOutputSize = state->screenSize;

		const float fullOutputWidth = globals::game::isVR ? (fullOutputSize.x * 0.5f) : fullOutputSize.x;
		const float fullOutputHeight = fullOutputSize.y;
		if (fullOutputWidth > 0.0f && fullOutputHeight > 0.0f) {
			viewportScaleX = std::clamp(static_cast<float>(extentOut.width) / fullOutputWidth, 1e-4f, 1.0f);
			viewportScaleY = std::clamp(static_cast<float>(extentOut.height) / fullOutputHeight, 1e-4f, 1.0f);
		}
	}

	const bool colorBuffersHDR = GetDLSSColorBuffersHDR(colorIn);
	const bool useExistingDLSSProfile =
		existingProvider.valid &&
		existingProvider.method == Upscaling::UpscaleMethod::kDLSS;
	uint32_t qualityMode = 0;
	uint32_t dlssPreset = Upscaling::kDLSSPresetK;
	if (useAuthoritativeProfile) {
		qualityMode = std::min(authoritativeQualityMode, Upscaling::kQualityModeMaxIndex);
		dlssPreset = Upscaling::ClampDLSSPresetUInt(authoritativeDLSSPreset);
	} else if (useExistingDLSSProfile) {
		qualityMode = existingProvider.qualityMode;
		dlssPreset = existingProvider.dlssPreset;
	} else {
		qualityMode = std::min(upscaling.GetRuntimeQualityMode(), Upscaling::kQualityModeMaxIndex);
		dlssPreset = upscaling.GetRuntimeDLSSPreset();
	}
	const sl::ViewportHandle requestedViewport = vp;
	const bool submitStageVRDLSS =
		globals::game::isVR &&
		upscaling.IsPresentationUpscalingActive();

	const bool collectDLSSDiagnostics = ShouldLogDLSSDiagnostics();
	DLSSDispatchDiagnostics diagnostics{};
	DLSSDispatchDiagnostics* diagnosticsPtr = &diagnostics;
	diagnostics.label = label ? label : "DLSS Evaluate";
	diagnostics.frame = state ? state->frameCount : 0u;
	diagnostics.eyeIndex = eyeIndex;
	diagnostics.requestedViewport = requestedViewport;
	diagnostics.resolvedViewport = vp;
	diagnostics.extentIn = extentIn;
	diagnostics.extentOut = extentOut;
	diagnostics.outputWidth = outputWidth;
	diagnostics.outputHeight = extentOut.height;
	diagnostics.qualityMode = qualityMode;
	diagnostics.dlssPreset = dlssPreset;
	diagnostics.viewportRole = viewportRole;
	diagnostics.viewportScaleX = viewportScaleX;
	diagnostics.viewportScaleY = viewportScaleY;
	diagnostics.croppedViewport = viewportScaleX < 0.999f || viewportScaleY < 0.999f;
	diagnostics.pinholeOffsetX = pinholeOffsetX;
	diagnostics.pinholeOffsetY = pinholeOffsetY;
	diagnostics.submitStageVRDLSS = submitStageVRDLSS;
	diagnostics.colorIn = colorIn;
	diagnostics.colorOut = colorOut;
	diagnostics.depth = depth;
	diagnostics.motionVectors = mvec;
	diagnostics.reactiveMask = reactiveMask;
	diagnostics.transparencyMask = transparencyMask;
	if (collectDLSSDiagnostics) {
		diagnostics.jitterX = upscaling.jitter.x;
		diagnostics.jitterY = upscaling.jitter.y;
		diagnostics.colorBuffersHDR = colorBuffersHDR;
		diagnostics.presentationUpscalingActive = upscaling.IsPresentationUpscalingActive();
		diagnostics.renderScaleActive = upscaling.IsVRRenderScaleModeActive();
		diagnostics.foveatedDispatchEnabled = upscaling.IsFoveatedVendorDispatchEnabled(upscaling.GetRuntimeUpscaleMethod());
		diagnostics.peripheryTAAEnabled = upscaling.IsPeripheryTAAEnabled(upscaling.GetRuntimeUpscaleMethod());
		diagnostics.historyResetRequested = upscaling.ShouldResetHistoryThisFrame();
		diagnostics.frameToken = frameToken;
	}
	const auto updateOptionsCacheDiagnostics = [&]() {
		if (!collectDLSSDiagnostics)
			return;

		const auto& optionsCache = GetDLSSOptionsCache(viewportRole, eyeIndex, qualityMode, dlssPreset);
		diagnostics.optionsCacheValid = optionsCache.valid;
		diagnostics.optionsCacheViewport = optionsCache.viewport;
		diagnostics.optionsCacheOutputWidth = optionsCache.outputWidth;
		diagnostics.optionsCacheOutputHeight = optionsCache.outputHeight;
		diagnostics.optionsCacheQualityMode = optionsCache.qualityMode;
		diagnostics.optionsCacheDLSSPreset = optionsCache.dlssPreset;
		diagnostics.optionsCacheHDR = optionsCache.isHDR;
		diagnostics.optionsCacheLegacyProfile = optionsCache.useLegacyProfile;
	};

	if (existingProviderOnly) {
		if (!TryResolveExistingVRDLSSViewport(
				viewportRole,
				eyeIndex,
				qualityMode,
				dlssPreset,
				outputWidth,
				extentOut.height,
				colorIn,
				vp)) {
			LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::ResolveViewport, "lifecycle-gated", diagnosticsPtr);
			return false;
		}
	} else if (!ResolveDLSSViewport(viewportRole, vp, eyeIndex, qualityMode, dlssPreset, vp)) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::ResolveViewport, "unavailable", diagnosticsPtr);
		return false;
	}
	diagnostics.resolvedViewport = vp;
	updateOptionsCacheDiagnostics();

	if (!CheckFrameConstants(vp, eyeIndex, viewportScaleX, viewportScaleY, pinholeOffsetX, pinholeOffsetY, diagnosticsPtr))
		return false;
	if (!existingProviderOnly &&
		!SetDLSSOptions(viewportRole, vp, eyeIndex, outputWidth, extentOut.height, colorBuffersHDR, qualityMode, dlssPreset, diagnosticsPtr))
		return false;
	updateOptionsCacheDiagnostics();

	const bool emitPCLMarkers =
		!submitStageVRDLSS &&
		upscaling.settings.reflexUseMarkersToOptimize &&
		reflexOptionsCache.useMarkersToOptimize &&
		featurePCL;
	const auto emitPCLMarker = [&](sl::PCLMarker marker, const char* stageName, uint32_t stageIndex) {
		if (!emitPCLMarkers || !slPCLSetMarker || !frameToken)
			return;
		const sl::Result markerResult = slPCLSetMarker(marker, *frameToken);
		if (markerResult != sl::Result::eOk) {
			static bool markerErrorLogged[2][2] = { { false, false }, { false, false } };
			const uint32_t logIdx = globals::game::isVR ? std::min(eyeIndex, 1u) : 0u;
			const uint32_t boundedStageIndex = std::min(stageIndex, 1u);
			if (markerErrorLogged[logIdx][boundedStageIndex])
				return;
			markerErrorLogged[logIdx][boundedStageIndex] = true;
			logger::warn(
				"[Streamline] slPCLSetMarker({}) failed{}: {}",
				stageName,
				globals::game::isVR ? std::format(" for eye {}", eyeIndex) : "",
				magic_enum::enum_name(markerResult));
		}
	};

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn }
	};

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = {
		&view,
		&tags[0],
		&tags[1],
		&tags[2],
		&tags[3],
		&tags[4],
		&tags[5]
	};

	if (state && state->frameAnnotations) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "DLSS Evaluate Eye %u", eyeIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("DLSS Evaluate");
		}
	}

	emitPCLMarker(sl::PCLMarker::eRenderSubmitStart, "DLSS-EvaluateStart", 0);
	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);
	emitPCLMarker(sl::PCLMarker::eRenderSubmitEnd, "DLSS-EvaluateEnd", 1);

	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::Evaluate, evalResult, diagnosticsPtr);
		static sl::ViewportHandle lastLoggedEvalErrorViewport[2] = {};
		static sl::Result lastLoggedEvalErrorResult[2] = {};
		uint32_t logIdx = globals::game::isVR ? std::min(eyeIndex, 1u) : 0;
		if (lastLoggedEvalErrorViewport[logIdx] != vp || lastLoggedEvalErrorResult[logIdx] != evalResult) {
			lastLoggedEvalErrorViewport[logIdx] = vp;
			lastLoggedEvalErrorResult[logIdx] = evalResult;
			D3D11_TEXTURE2D_DESC colorInDesc{};
			D3D11_TEXTURE2D_DESC colorOutDesc{};
			TryGetTexture2DDesc(colorIn, colorInDesc);
			TryGetTexture2DDesc(colorOut, colorOutDesc);
			logger::error(
				"[Streamline] slEvaluateFeature failed{} result={} viewport={} colorIn={}x{} fmt={} colorOut={}x{} fmt={} extentIn={}x{} extentOut={}x{}",
				globals::game::isVR ? std::format(" for eye {}", eyeIndex) : "",
				static_cast<int>(evalResult),
				static_cast<uint32_t>(vp),
				colorInDesc.Width,
				colorInDesc.Height,
				static_cast<uint32_t>(colorInDesc.Format),
				colorOutDesc.Width,
				colorOutDesc.Height,
				static_cast<uint32_t>(colorOutDesc.Format),
				extentIn.width,
				extentIn.height,
				extentOut.width,
				extentOut.height);
		}
	}

	return evalResult == sl::Result::eOk;
}

bool Streamline::UpscaleRegion(uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
	float pinholeOffsetX, float pinholeOffsetY)
{
	if (!initialized || !featureDLSS || !colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask)
		return false;

	sl::ViewportHandle vp = (globals::game::isVR && eyeIndex == 1) ? viewportRight : viewport;
	sl::Extent extentIn{ 0u, 0u, renderWidth, renderHeight };
	sl::Extent extentOut{ 0u, 0u, outputWidth, outputHeight };

	return EvaluateDLSS(vp, eyeIndex, colorIn, colorOut, depth, mvec, reactiveMask, transparencyMask, extentIn, extentOut, outputWidth, pinholeOffsetX, pinholeOffsetY, "UpscaleRegion");
}

bool Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
{
	auto state = globals::state;

	auto renderer = globals::game::renderer;
	if (!state || !renderer)
		return false;

	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto& upscaling = globals::features::upscaling;
	if (globals::game::isVR && upscaling.IsPresentationUpscalingActive()) {
		upscaling.dlssUpscaleOutputInSharpenerTexture = false;
		return false;
	}

	auto screenSize = upscaling.GetRuntimeResolutionPlan().finalOutputSize;
	auto renderSize = upscaling.GetRuntimeResolutionPlan().engineRenderSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
		screenSize = state->screenSize;
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		renderSize = Util::ConvertToDynamic(screenSize);
	auto& mainTarget = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	const bool isVR = globals::game::isVR;
	const bool sharpenerOutputReady =
		upscaling.sharpenerTexture &&
		upscaling.sharpenerTexture->resource &&
		(isVR || upscaling.sharpenerTexture->resource.get() != a_upscalingTexture) &&
		(!isVR || upscaling.sharpenerTexture->uav);

	// Flat DLSS receives kMAIN as its color input, so writing directly back to it
	// would alias the Streamline input and output tags. VR first isolates each eye's
	// input and can retain its direct combined-target path when sharpening is off.
	if (!isVR) {
		static bool loggedMissingSharpenerOutput = false;
		if (!sharpenerOutputReady) {
			if (!loggedMissingSharpenerOutput) {
				logger::error("[Upscaling] DLSS dispatch skipped because a distinct intermediate output is unavailable.");
				loggedMissingSharpenerOutput = true;
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}
		loggedMissingSharpenerOutput = false;
	}

	const bool useSharpenerOutput =
		sharpenerOutputReady &&
		upscaling.ShouldRouteDLSSMainPassThroughSharpener();
	ID3D11Resource* colorOut = useSharpenerOutput ? upscaling.sharpenerTexture->resource.get() : a_upscalingTexture;
	ID3D11UnorderedAccessView* colorOutUAV = useSharpenerOutput ? upscaling.sharpenerTexture->uav.get() : mainTarget.UAV;
	const bool outputToSharpener = useSharpenerOutput;

	// VR: Combined-buffer mode with extent offsets causes temporal ghosting on the right eye
	// because DLSS's internal history buffers use extent offsets as indices.
	// Per-eye isolation with extents at {0,0} is required.
	if (globals::game::isVR) {
		auto context = globals::d3d::context;
		uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
		uint32_t eyeHeightOut = (uint32_t)screenSize.y;
		uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
		uint32_t eyeHeightIn = (uint32_t)renderSize.y;
		const uint32_t contractGeneration =
			upscaling.IsVRRenderScaleModeLatched() ?
				upscaling.GetActiveVRRenderScaleContractGeneration() : 0u;
		const bool vendorLifecycleMutationDeferred =
			upscaling.ShouldDeferVRVendorLifecycleMutation();
		const auto existingProvider =
			vendorLifecycleMutationDeferred ?
				upscaling.GetExistingVRVendorProviderSnapshot() :
				Upscaling::VRExistingVendorProviderSnapshot{};
		if (vendorLifecycleMutationDeferred &&
			!upscaling.AreActiveVRIntermediateTexturesCompatible(
				Upscaling::UpscaleMethod::kDLSS,
				eyeWidthIn,
				eyeHeightIn,
				eyeWidthOut,
				eyeHeightOut,
				a_upscalingTexture,
				a_motionVectors,
				a_reactiveMask,
				a_transparencyCompositionMask,
				contractGeneration)) {
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}
		if (vendorLifecycleMutationDeferred) {
			if (!existingProvider.valid ||
				existingProvider.method != Upscaling::UpscaleMethod::kDLSS ||
				existingProvider.renderEyeWidth != eyeWidthIn ||
				existingProvider.renderEyeHeight != eyeHeightIn ||
				existingProvider.displayEyeWidth != eyeWidthOut ||
				existingProvider.displayEyeHeight != eyeHeightOut) {
				upscaling.dlssUpscaleOutputInSharpenerTexture = false;
				return false;
			}
			const uint32_t qualityMode = existingProvider.qualityMode;
			const uint32_t dlssPreset = existingProvider.dlssPreset;
			for (uint32_t eye = 0; eye < 2; ++eye) {
				sl::ViewportHandle resolvedViewport{};
				if (!TryResolveExistingVRDLSSViewport(
						DLSSViewportRole::FullEye,
						eye,
						qualityMode,
						dlssPreset,
						eyeWidthOut,
						eyeHeightOut,
						upscaling.vrIntermediateColorIn[eye]->resource.get(),
						resolvedViewport)) {
					upscaling.dlssUpscaleOutputInSharpenerTexture = false;
					return false;
				}
			}
		}
		const bool useAuthoritativeExistingProfile =
			vendorLifecycleMutationDeferred &&
			existingProvider.valid &&
			existingProvider.method == Upscaling::UpscaleMethod::kDLSS;
		const uint32_t authoritativeQualityMode =
			useAuthoritativeExistingProfile ? existingProvider.qualityMode : 0u;
		const uint32_t authoritativeDLSSPreset =
			useAuthoritativeExistingProfile ?
				existingProvider.dlssPreset :
				Upscaling::kDLSSPresetK;

		// Split the combined stereo inputs up front. The direct left-eye path still
		// uses the native depth buffer, but isolated-output fallback needs valid
		// per-eye depth for both eyes.
		if (!upscaling.PreparePerEyeInputs(
				a_upscalingTexture,
				depthTexture.texture,
				a_motionVectors,
				a_reactiveMask,
				a_transparencyCompositionMask,
				false,
				true)) {
			static bool loggedPrepareFailure = false;
			if (!loggedPrepareFailure) {
				logger::warn("[Streamline] VR DLSS/DLAA skipped because per-eye input preparation failed.");
				loggedPrepareFailure = true;
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}

		const bool perEyeResourcesReady = upscaling.AreVRPerEyeUpscalingResourcesReady(true, false);
		if (!perEyeResourcesReady) {
			static bool loggedMissingResource = false;
			if (!loggedMissingResource) {
				logger::warn("[Streamline] VR DLSS/DLAA skipped because prepared per-eye resources are incomplete.");
				loggedMissingResource = true;
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}

		sl::Extent extentIn{ 0, 0, eyeWidthIn, eyeHeightIn };
		sl::Extent extentOut{ 0, 0, eyeWidthOut, eyeHeightOut };
		auto presentStretchFallback = [&]() {
			bool stretched = true;
			for (uint32_t i = 0; i < 2; ++i)
				stretched = upscaling.StretchSubmitStageEyeOutput(i, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut) && stretched;
			if (stretched)
				upscaling.FinalizePerEyeOutputs(colorOut);
			return stretched;
		};

		const bool canUseDirectEye0 =
			perEyeResourcesReady;

		if (!canUseDirectEye0) {
			bool allEvaluated = true;
			for (uint32_t i = 0; i < 2; ++i) {
				sl::ViewportHandle vp = (i == 1) ? viewportRight : viewport;
				const bool eyeEvaluated = EvaluateDLSS(vp, i,
					upscaling.vrIntermediateColorIn[i]->resource.get(), upscaling.vrIntermediateColorOut[i]->resource.get(),
					upscaling.vrIntermediateDepth[i]->resource.get(), upscaling.vrIntermediateMotionVectors[i]->resource.get(),
					upscaling.vrIntermediateReactiveMask[i]->resource.get(), upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
					extentIn, extentOut, eyeWidthOut,
					0.0f,
					0.0f,
					"VR prepared per-eye",
					DLSSViewportRole::FullEye,
					useAuthoritativeExistingProfile,
					authoritativeQualityMode,
					authoritativeDLSSPreset);
				upscaling.RecordVRDLSSFullEyeEvaluation(i, eyeEvaluated);
				allEvaluated &= eyeEvaluated;
			}

			bool fallbackPresented = false;
			if (allEvaluated) {
				upscaling.FinalizePerEyeOutputs(colorOut);
			} else {
				upscaling.RequestHistoryReset();
				fallbackPresented = presentStretchFallback();
				static bool loggedVREvaluateFailure = false;
				static bool loggedVRStretchFallbackFailure = false;
				if (fallbackPresented) {
					if (!loggedVREvaluateFailure) {
						logger::warn("[Streamline] VR DLSS/DLAA evaluate did not complete for both eyes; using full-size stretch fallback for this frame.");
						loggedVREvaluateFailure = true;
					}
				} else if (!loggedVRStretchFallbackFailure) {
					logger::warn("[Streamline] VR DLSS/DLAA evaluate did not complete for both eyes and stretch fallback failed; keeping the current scene texture.");
					loggedVRStretchFallbackFailure = true;
				}
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && (allEvaluated || fallbackPresented);
			return allEvaluated;
		}

		// Copy right-eye depth before eye 0 evaluation; eye 0 output can overlap right-eye input
		// in the combined target at non-DLAA scales.
		D3D11_BOX rightIn = { eyeWidthIn, 0, 0, eyeWidthIn * 2, eyeHeightIn, 1 };
		context->CopySubresourceRegion(upscaling.vrIntermediateDepth[1]->resource.get(), 0, 0, 0, 0, depthTexture.texture, 0, &rightIn);
		const bool canRestoreDirectEye0Output =
			!outputToSharpener &&
			upscaling.vrIntermediateColorOut[0] &&
			upscaling.vrIntermediateColorOut[0]->resource;
		if (canRestoreDirectEye0Output) {
			D3D11_BOX leftOutBackup = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
			context->CopySubresourceRegion(upscaling.vrIntermediateColorOut[0]->resource.get(), 0, 0, 0, 0, colorOut, 0, &leftOutBackup);
		}

		// Eye 0 writes directly to combined output.
		const bool leftEvaluated = EvaluateDLSS(viewport, 0,
			upscaling.vrIntermediateColorIn[0]->resource.get(), colorOut,
			depthTexture.texture, upscaling.vrIntermediateMotionVectors[0]->resource.get(),
			upscaling.vrIntermediateReactiveMask[0]->resource.get(), upscaling.vrIntermediateTransparencyMask[0]->resource.get(),
			extentIn, extentOut, eyeWidthOut,
			0.0f,
			0.0f,
			"VR direct eye0 combined",
			DLSSViewportRole::FullEye,
			useAuthoritativeExistingProfile,
			authoritativeQualityMode,
			authoritativeDLSSPreset);
		upscaling.RecordVRDLSSFullEyeEvaluation(0, leftEvaluated);

		// Eye 1 writes to intermediate, then copy into right half of combined output.
		const bool rightEvaluated = EvaluateDLSS(viewportRight, 1,
			upscaling.vrIntermediateColorIn[1]->resource.get(), upscaling.vrIntermediateColorOut[1]->resource.get(),
			upscaling.vrIntermediateDepth[1]->resource.get(), upscaling.vrIntermediateMotionVectors[1]->resource.get(),
			upscaling.vrIntermediateReactiveMask[1]->resource.get(), upscaling.vrIntermediateTransparencyMask[1]->resource.get(),
			extentIn, extentOut, eyeWidthOut,
			0.0f,
			0.0f,
			"VR direct eye1 intermediate",
			DLSSViewportRole::FullEye,
			useAuthoritativeExistingProfile,
			authoritativeQualityMode,
			authoritativeDLSSPreset);
		upscaling.RecordVRDLSSFullEyeEvaluation(1, rightEvaluated);

		if (leftEvaluated && rightEvaluated) {
			if (depthTexture.depthSRV) {
				upscaling.ClearVRDirectUpscaledEyeOutput(0, colorOutUAV, depthTexture.depthSRV, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
				upscaling.ClearVRDirectUpscaledEyeOutput(1, upscaling.vrIntermediateColorOut[1]->uav.get(), depthTexture.depthSRV, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
			}

			D3D11_BOX rightOut = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
			context->CopySubresourceRegion(colorOut, 0, eyeWidthOut, 0, 0, upscaling.vrIntermediateColorOut[1]->resource.get(), 0, &rightOut);
		}

		bool fallbackPresented = false;
		if (!leftEvaluated || !rightEvaluated) {
			upscaling.RequestHistoryReset();
			fallbackPresented = presentStretchFallback();
			static bool loggedVRDirectEvaluateFailure = false;
			static bool loggedVRDirectStretchFallbackFailure = false;
			if (fallbackPresented) {
				if (!loggedVRDirectEvaluateFailure) {
					logger::warn("[Streamline] VR DLSS/DLAA direct-eye evaluate failed; using full-size stretch fallback for this frame.");
					loggedVRDirectEvaluateFailure = true;
				}
			} else if (!loggedVRDirectStretchFallbackFailure) {
				logger::warn("[Streamline] VR DLSS/DLAA direct-eye evaluate failed and stretch fallback failed; keeping the current scene texture.");
				loggedVRDirectStretchFallbackFailure = true;
			}
			if (!fallbackPresented && canRestoreDirectEye0Output) {
				D3D11_BOX leftOutBackup = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
				context->CopySubresourceRegion(colorOut, 0, 0, 0, 0, upscaling.vrIntermediateColorOut[0]->resource.get(), 0, &leftOutBackup);
			}
		}

		upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && ((leftEvaluated && rightEvaluated) || fallbackPresented);
		return leftEvaluated && rightEvaluated;

	} else {
		// Non-VR: Simple full-texture upscale
		sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
		sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

		const bool evaluated = EvaluateDLSS(viewport, 0,
			a_upscalingTexture, colorOut,
			depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
			extentIn, extentOut, (uint)screenSize.x,
			0.0f,
			0.0f,
			"Non-VR main");
		upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && evaluated;
		if (!evaluated) {
			upscaling.RequestHistoryReset();
			static bool loggedEvaluateFailure = false;
			if (!loggedEvaluateFailure) {
				logger::warn("[Streamline] DLSS/DLAA evaluate failed; keeping the current scene texture instead of sharpening stale output.");
				loggedEvaluateFailure = true;
			}
		}
		return evaluated;
	}
}
/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
Streamline::DLSSResourceTeardownResult Streamline::DestroyDLSSResources()
{
	const bool hasTrackedViewportOwnership = [&]() {
		if (activeDLSSViewportResourcesAllocated[0] ||
			activeDLSSViewportResourcesAllocated[1] ||
			nonVRDLSSOptionsCache.valid) {
			return true;
		}
		for (const auto& roleSlots : vrDLSSViewportSlots) {
			for (const auto& slot : roleSlots) {
				if (slot.valid ||
					slot.resourcesAllocated[0] ||
					slot.resourcesAllocated[1] ||
					slot.optionsCache[0].valid ||
					slot.optionsCache[1].valid) {
					return true;
				}
			}
		}
		return false;
	}();
	if (!initialized || !featureDLSS || !slDLSSSetOptions || !slFreeResources) {
		ResetDLSSIdleFences();
		if (hasTrackedViewportOwnership) {
			static bool loggedUnavailableTeardownOwnership = false;
			if (!loggedUnavailableTeardownOwnership) {
				logger::error("[Streamline] Refusing to report DLSS teardown complete while tracked viewport ownership cannot be released.");
				loggedUnavailableTeardownOwnership = true;
			}
			return DLSSResourceTeardownResult::Failed;
		}
		InvalidateDLSSOptionsCache();
		activeDLSSViewportResourcesAllocated = {};
		ResetFrameTracking();
		return DLSSResourceTeardownResult::Ready;
	}

	if (auto context = globals::d3d::context) {
		const auto idleFenceResult = BeginOrPollD3D11IdleFence(context, pendingDLSSResourceFreeIdleFence, "DLSS resource free");
		if (idleFenceResult == D3D11IdleFenceResult::Pending) {
			static bool loggedDLSSResourceFreePending = false;
			if (!loggedDLSSResourceFreePending) {
				logger::warn("[Streamline] Deferring DLSS resource free because the D3D11 queue did not become idle.");
				loggedDLSSResourceFreePending = true;
			}
			return DLSSResourceTeardownResult::Pending;
		}
		if (idleFenceResult == D3D11IdleFenceResult::Failed)
			return DLSSResourceTeardownResult::Failed;
	} else {
		ResetDLSSIdleFences();
	}

	bool activeViewportResourcesFreed = true;
	if (activeDLSSViewportResourcesAllocated[0]) {
		const bool leftFreed = FreeDLSSViewportResources(viewport, 0, true);
		activeViewportResourcesFreed = leftFreed && activeViewportResourcesFreed;
		if (leftFreed)
			activeDLSSViewportResourcesAllocated[0] = false;
	}

	if (globals::game::isVR) {
		if (activeDLSSViewportResourcesAllocated[1]) {
			const bool rightFreed = FreeDLSSViewportResources(viewportRight, 1, true);
			activeViewportResourcesFreed = rightFreed && activeViewportResourcesFreed;
			if (rightFreed)
				activeDLSSViewportResourcesAllocated[1] = false;
		}
		for (uint32_t roleIndex = 0; roleIndex < kVRDLSSViewportRoleCount; ++roleIndex) {
			for (uint32_t slotIndex = 0; slotIndex < kVRDLSSViewportSlotCount; ++slotIndex) {
				const bool slotFreed = FreeVRDLSSViewportSlot(static_cast<DLSSViewportRole>(roleIndex), slotIndex, false);
				activeViewportResourcesFreed = slotFreed && activeViewportResourcesFreed;
			}
		}
	}

	ResetDLSSIdleFences();
	InvalidateDLSSOptionsCache();
	vrDLSSViewportUseCounter = 0;
	ResetFrameTracking();
	return activeViewportResourcesFreed ?
	           DLSSResourceTeardownResult::Ready :
	           DLSSResourceTeardownResult::FailedAfterMutation;
}

void Streamline::UpdateReflex()
{
	if (!initialized || !reflexSupportedOnCurrentAdapter || !featureReflex || !slReflexSetOptions)
		return;

	const auto& upscaling = globals::features::upscaling;
	const bool reflexBlockedByFrameGeneration = upscaling.IsFrameGenerationDx12PathActive();
	if (reflexBlockedByFrameGeneration) {
		const bool reflexAlreadyOff = reflexOptionsCache.valid &&
		                              reflexOptionsCache.mode == sl::ReflexMode::eOff &&
		                              reflexOptionsCache.frameLimitUs == 0 &&
		                              !reflexOptionsCache.useMarkersToOptimize;
		if (!reflexAlreadyOff) {
			sl::ReflexOptions disableOptions{};
			disableOptions.mode = sl::ReflexMode::eOff;
			disableOptions.frameLimitUs = 0;
			disableOptions.useMarkersToOptimize = false;
			if (SL_FAILED(result, slReflexSetOptions(disableOptions))) {
				logger::error("[Streamline] Failed to disable Reflex while Frame Generation is active: {}", magic_enum::enum_name(result));
			} else {
				reflexOptionsCache.valid = true;
				reflexOptionsCache.mode = disableOptions.mode;
				reflexOptionsCache.frameLimitUs = disableOptions.frameLimitUs;
				reflexOptionsCache.useMarkersToOptimize = disableOptions.useMarkersToOptimize;
			}
		}
		lastReflexSleepFrame = UINT32_MAX;
		return;
	}

	auto& settings = globals::features::upscaling.settings;

	sl::ReflexOptions options{};
	if (!settings.reflexLowLatencyMode) {
		options.mode = sl::ReflexMode::eOff;
	} else {
		options.mode = settings.reflexLowLatencyBoost ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
	}

	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	float reflexFPSLimit = originalReflexFPSLimit;
	if (!std::isfinite(reflexFPSLimit)) {
		reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = reflexFPSLimit;
		logger::warn("[Streamline] reflexFPSLimit is not finite ({}), using {}", originalReflexFPSLimit, reflexFPSLimit);
	}
	const float fpsLimit = std::clamp(reflexFPSLimit, 20.0f, 240.0f);
	options.frameLimitUs = settings.reflexUseFPSLimit ? static_cast<uint32_t>(std::lround(1000000.0 / static_cast<double>(fpsLimit))) : 0u;
	options.useMarkersToOptimize = settings.reflexUseMarkersToOptimize && featurePCL;

	if (!reflexOptionsCache.valid ||
		reflexOptionsCache.mode != options.mode ||
		reflexOptionsCache.frameLimitUs != options.frameLimitUs ||
		reflexOptionsCache.useMarkersToOptimize != options.useMarkersToOptimize) {
		if (SL_FAILED(result, slReflexSetOptions(options))) {
			logger::error("[Streamline] Failed to apply Reflex options: {}", magic_enum::enum_name(result));
		} else {
			reflexOptionsCache.valid = true;
			reflexOptionsCache.mode = options.mode;
			reflexOptionsCache.frameLimitUs = options.frameLimitUs;
			reflexOptionsCache.useMarkersToOptimize = options.useMarkersToOptimize;
		}
	}

	if (!slReflexSleep)
		return;

	if (options.mode == sl::ReflexMode::eOff && options.frameLimitUs == 0)
		return;

	const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0;
	if (lastReflexSleepFrame == currentFrame)
		return;

	if (!EnsureFrameToken())
		return;

	lastReflexSleepFrame = currentFrame;
	if (SL_FAILED(result, slReflexSleep(*frameToken))) {
		logger::warn("[Streamline] Reflex sleep call failed: {}", magic_enum::enum_name(result));
	}
}
