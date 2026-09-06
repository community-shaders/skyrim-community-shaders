#include "Streamline.h"

#include "DXVKInterop.h"

#include "../../DxvkLoader.h"
#include "../../Globals.h"
#include "../../State.h"
#include "../../Utils/Game.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <unordered_set>

#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4471 5103)
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_device_wrappers.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_fsr.h>
#include <sl_fsr_g.h>
#include <sl_xess.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

namespace
{
	struct SLState
	{
		HMODULE interposer = nullptr;

		PFun_slInit* slInit = nullptr;
		PFun_slIsFeatureSupported* slIsFeatureSupported = nullptr;
		PFun_slGetNewFrameToken* slGetNewFrameToken = nullptr;
		PFun_slSetTagForFrame* slSetTagForFrame = nullptr;
		PFun_slSetConstants* slSetConstants = nullptr;
		PFun_slEvaluateFeature* slEvaluateFeature = nullptr;
		PFun_slGetFeatureFunction* slGetFeatureFunction = nullptr;
		PFun_slSetFeatureLoaded* slSetFeatureLoaded = nullptr;

		PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings = nullptr;
		PFun_slDLSSSetOptions* slDLSSSetOptions = nullptr;
		PFun_slReflexSetOptions* slReflexSetOptions = nullptr;
		PFun_slReflexSleep* slReflexSleep = nullptr;
		PFun_slPCLSetMarker* slPCLSetMarker = nullptr;
		PFun_slDLSSGSetOptions* slDLSSGSetOptions = nullptr;
		PFun_slDLSSGGetState* slDLSSGGetState = nullptr;
		PFun_slFSRSetOptions* slFSRSetOptions = nullptr;
		PFun_slFSRFrameGenerationSetOptions* slFSRFrameGenerationSetOptions = nullptr;
		PFun_slFSRGetFrameGenState* slFSRGetFrameGenState = nullptr;
		PFun_slFSRFrameGenerationDiscardPreparedFrame* slFSRFrameGenerationDiscardPreparedFrame = nullptr;
		PFun_slFSRFrameGenerationOwnsSwapchain* slFSRFrameGenerationOwnsSwapchain = nullptr;
		PFun_slFSRFrameGenerationCompleteSwapchainTeardown* slFSRFrameGenerationCompleteSwapchainTeardown = nullptr;
		PFun_slXeSSSetOptions* slXeSSSetOptions = nullptr;

		sl::ViewportHandle viewport{ 0 };

		std::atomic<uint32_t> renderFrameId = { 0 };
		// Frame index SimulationStart used, latched so tags, constants and the render-thread PCL
		// markers all agree with it. See Streamline::BeginRenderFrame.
		std::atomic<uint32_t> simMarkerFrameId = { 0u };
		// PCL markers only feed Reflex and DLSS-G latency. With neither active, emitting them is pure
		// per-frame cross-DLL overhead -- 7 slGetNewFrameToken+slPCLSetMarker pairs per frame on a path
		// the D3D11 branch does not have at all. Streamline is now always initialised for in-game
		// switching, so this has to be gated on use rather than on initialisation.
		std::atomic<bool> latencyMarkersNeeded = { false };

		// Disable dispatch after an SEH fault to prevent repeated crashes.
		std::atomic<bool> dispatchFaulted{ false };

		bool reflexCacheValid = false;
		sl::ReflexMode reflexCachedMode = sl::ReflexMode::eOff;
		uint32_t reflexCachedFrameLimitUs = 0;

		bool dlssgModeCached = false;
		bool dlssgModeOn = false;
		uint32_t dlssgCachedDisplayW = 0, dlssgCachedDisplayH = 0;
		uint32_t dlssgCachedNumFrames = 0;
		bool dlssgCachedAuto = false;
		bool dlssgCachedDynamic = false;
		float dlssgCachedDynamicFps = 0.0f;
		std::atomic<uint32_t> dlssgMaxFramesToGenerate = 0;
		std::atomic<bool> dlssgDynamicSupported = false;
		std::atomic<uint32_t> frameGenerationMultiplier = 1;
		// Running total of frames the FSR-FG swapchain has presented (real + generated). Differenced
		// over time this is the true post-FG frame rate; the per-present multiplier above reads 1 on
		// any present that had no prepared frame, so sampling it reports an un-doubled value.
		std::atomic<uint64_t> fsrTotalPresentedFrames = 0;

		// Present requires either a valid or passthrough tag every frame.
		bool dlssgTaggedThisFrame = false;
		std::atomic<bool> dlssgCloneTagsPrimed{ false };
	} g_sl;

	// All DXVK exports used by Streamline, resolved once at device setup.
	struct DxvkExports
	{
		using RequestSwapchainRecreateFn = void (*)();
		using SetSyncPresentFn = void (*)(uint32_t);
		using SetSwapchainTornDownCallbackFn = void (*)(bool (*)());
		using SetPresentQueueDepthFn = void (*)(uint32_t);
		using SetTearingPreferenceFn = void (*)(uint32_t);

		HMODULE module = nullptr;
		RequestSwapchainRecreateFn requestSwapchainRecreate = nullptr;
		SetSyncPresentFn setSyncPresent = nullptr;
		bool presenterSurfaceStateAvailable = false;
		SetSwapchainTornDownCallbackFn setSwapchainTornDownCallback = nullptr;
		SetPresentQueueDepthFn setPresentQueueDepth = nullptr;
		SetTearingPreferenceFn setTearingPreference = nullptr;

		void Resolve()
		{
			module = GetModuleHandleW(L"dxvk_d3d11.dll");
			if (!module)
				return;
			requestSwapchainRecreate = reinterpret_cast<RequestSwapchainRecreateFn>(
				GetProcAddress(module, "dxvkRequestSwapchainRecreate"));
			setSyncPresent = reinterpret_cast<SetSyncPresentFn>(
				GetProcAddress(module, "dxvkSetSyncPresent"));
			presenterSurfaceStateAvailable =
				GetProcAddress(module, "dxvkGetPresenterSurfaceState") != nullptr;
			setSwapchainTornDownCallback = reinterpret_cast<SetSwapchainTornDownCallbackFn>(
				GetProcAddress(module, "dxvkSetSwapchainTornDownCallback"));
			setPresentQueueDepth = reinterpret_cast<SetPresentQueueDepthFn>(
				GetProcAddress(module, "dxvkSetPresentQueueDepth"));
			setTearingPreference = reinterpret_cast<SetTearingPreferenceFn>(
				GetProcAddress(module, "dxvkSetTearingPreference"));
		}

		[[nodiscard]] bool HasFrameGenerationInterop() const
		{
			return module && requestSwapchainRecreate && setSyncPresent &&
			       presenterSurfaceStateAvailable && setSwapchainTornDownCallback;
		}
	} g_dxvk;

	// Feature load changes are applied only while the swapchain is torn down.
	std::atomic<bool> g_dlssgDesiredLoaded{ false };
	std::atomic<bool> g_dlssgCurrentlyLoaded{ false };
	std::atomic<bool> g_fsrfgDesiredLoaded{ false };
	std::atomic<bool> g_fsrfgCurrentlyLoaded{ false };
	std::atomic<bool> g_fsrfgOwnsPresent{ false };

	// Keep this free of C++ unwinding because it executes inside __try.
	bool ReconcileFgFeatureLoad(sl::Feature a_feature, std::atomic<bool>& a_desired, std::atomic<bool>& a_current)
	{
		const bool want = a_desired.load(std::memory_order_acquire);
		if (want == a_current.load(std::memory_order_acquire))
			return true;
		if (!g_sl.slSetFeatureLoaded || g_sl.dispatchFaulted.load(std::memory_order_acquire))
			return false;
		__try {
			if (g_sl.slSetFeatureLoaded(a_feature, want) != sl::Result::eOk)
				return false;
			a_current.store(want, std::memory_order_release);
			if (a_feature == sl::kFeatureFSR_G && !want)
				g_fsrfgOwnsPresent.store(false, std::memory_order_release);
			if (want) {
				// Safe: render thread is blocked on the present lock during swapchain teardown/recreation.
				if (a_feature == sl::kFeatureDLSS_G) {
					g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSGSetOptions));
					g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(g_sl.slDLSSGGetState));
				} else if (a_feature == sl::kFeatureFSR_G) {
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationSetOptions));
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRGetFrameGenState", reinterpret_cast<void*&>(g_sl.slFSRGetFrameGenState));
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationDiscardPreparedFrame", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationDiscardPreparedFrame));
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationOwnsSwapchain", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationOwnsSwapchain));
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationCompleteSwapchainTeardown", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationCompleteSwapchainTeardown));
				}
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_sl.dispatchFaulted = true;
			return false;
		}
	}

	// Runs between DXVK swapchain destruction and creation.
	bool DxvkSwapchainTornDownCallback()
	{
		// A new swapchain gets a fresh frame-generation ownership latch, so any earlier verdict
		// that the presenter would not attach present-wait semaphores no longer applies.
		DXVKInterop::GetSingleton()->ResetPresentWaitUnattachedForSwapchain();
		if (g_sl.dispatchFaulted.load(std::memory_order_acquire)) {
			g_sl.dlssgModeCached = false;
			g_sl.dlssgModeOn = false;
			g_sl.dlssgCloneTagsPrimed.store(false, std::memory_order_release);
			g_dlssgCurrentlyLoaded.store(false, std::memory_order_release);
			g_fsrfgCurrentlyLoaded.store(false, std::memory_order_release);
			g_fsrfgOwnsPresent.store(false, std::memory_order_release);
			return true;
		}

		// Per-swapchain options and semaphores are invalid after teardown.
		g_sl.dlssgModeCached = false;
		g_sl.dlssgModeOn = false;

		g_sl.dlssgCloneTagsPrimed.store(false, std::memory_order_release);

		if (g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire)) {
			if (g_sl.slFSRFrameGenerationCompleteSwapchainTeardown) {
				bool teardownComplete = false;
				const bool releaseFeatureContext = !g_fsrfgDesiredLoaded.load(std::memory_order_acquire);
				__try {
					teardownComplete = g_sl.slFSRFrameGenerationCompleteSwapchainTeardown(releaseFeatureContext);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					g_sl.dispatchFaulted = true;
				}
				if (!teardownComplete)
					return false;
			} else {
				g_fsrfgCurrentlyLoaded.store(false, std::memory_order_release);
			}
		}

		const bool dlssgReconciled = ReconcileFgFeatureLoad(
			sl::kFeatureDLSS_G, g_dlssgDesiredLoaded, g_dlssgCurrentlyLoaded);
		const bool fsrfgReconciled = ReconcileFgFeatureLoad(
			sl::kFeatureFSR_G, g_fsrfgDesiredLoaded, g_fsrfgCurrentlyLoaded);
		return dlssgReconciled && fsrfgReconciled;
	}

	// Suppress only diagnostics that are both expected in this integration and repeated often
	// enough to drown the log. Anything reporting a feature degrading or failing outright stays
	// visible: those are exactly the lines that explain a silently non-working feature.
	//
	// Deliberately NOT suppressed, having each cost real debugging time:
	//   "is NOT supported, plugin will not function properly" — a hook Streamline could not
	//       install. Three of these fire on the DLSS-G path (CmdBindPipeline,
	//       CmdBindDescriptorSets, BeginCommandBuffer).
	//   "Invalid backbuffer resource extent"                  — a resource tagged with a 0x0
	//       extent, which sl.dlss_g sanitises and carries on from.
	bool IsBenignSLWarning(const char* a_msg)
	{
		if (!a_msg)
			return false;
		static constexpr const char* kBenign[] = {
			"setAsyncFrameMarker is not implemented",
			"RSync will not run because it was not initialized",
			"some DX/VK APIs were invoked before slInit",
			"reseting frame timer",
		};
		for (const char* needle : kBenign) {
			if (std::strstr(a_msg, needle))
				return true;
		}
		return false;
	}

	// Streamline repeats most warnings every frame. Emit each distinct message once so a real
	// diagnostic is visible without the log turning into a per-frame stream.
	bool ShouldLogSLWarningOnce(const char* a_msg)
	{
		static std::mutex s_mutex;
		static std::unordered_set<size_t> s_seen;
		const size_t key = std::hash<std::string_view>{}(a_msg);
		std::lock_guard<std::mutex> lock(s_mutex);
		return s_seen.insert(key).second;
	}

	void LogCallback(sl::LogType a_type, const char* a_msg)
	{
		static const bool s_verbose = [] {
			char v[2] = {};
			return GetEnvironmentVariableA("CS_SL_VERBOSE", v, sizeof(v)) && v[0] == 0x31;
		}();
		if (s_verbose) {
			logger::info("[Streamline/SL] {}", a_msg);
			return;
		}
		if (a_type == sl::LogType::eWarn && IsBenignSLWarning(a_msg))
			return;
		switch (a_type) {
		case sl::LogType::eError:
			logger::warn("[Streamline/SL] {}", a_msg);
			break;
		case sl::LogType::eWarn:
			// Previously logged at debug, which the default sink drops — so every surviving
			// Streamline warning was invisible anyway, and un-suppressing one changed nothing.
			if (ShouldLogSLWarningOnce(a_msg))
				logger::warn("[Streamline/SL] {}", a_msg);
			break;
		default:
			logger::trace("[Streamline/SL] {}", a_msg);
			break;
		}
	}

	std::filesystem::path GetStreamlineDir()
	{
		return DxvkLoader::GetRuntimeDir();
	}

	template <typename T>
	bool Resolve(T*& a_fn, const char* a_name)
	{
		a_fn = reinterpret_cast<T*>(GetProcAddress(g_sl.interposer, a_name));
		if (!a_fn)
			logger::warn("[Streamline] missing interposer export '{}'", a_name);
		return a_fn != nullptr;
	}
}

Streamline* Streamline::GetSingleton()
{
	static Streamline singleton;
	return &singleton;
}

void Streamline::PreloadInterposer()
{
	// Preload before DXVK creates VkInstance so its Vulkan loader aliases the interposer.
	if (g_sl.interposer)
		return;
	const auto slDir = GetStreamlineDir();
	if (slDir.empty())
		return;
	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	g_sl.interposer = LoadLibraryExW(interposerPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	logger::info("[Streamline] interposer preload for DXVK Vulkan interposition: {}",
		g_sl.interposer ? "mapped" : "FAILED (DXVK uses real driver)");
	if (!g_sl.interposer)
		return;
	// slInit must precede DXVK's VkInstance creation.
	Initialize();
}

// Probe with the system loader before slInit decides which FG plugin to load.
static bool ProbeDLSSGHardware()
{
	if (char v[2] = {}; GetEnvironmentVariableA("CS_FORCE_FSR_FG", v, sizeof(v)) && v[0] == '1') {
		logger::info("[Streamline] CS_FORCE_FSR_FG=1: hardware probe reports no DLSS-G (FSR-FG path forced)");
		return false;
	}

	wchar_t sysDir[MAX_PATH]{};
	if (!GetSystemDirectoryW(sysDir, MAX_PATH))
		return false;
	const auto vkPath = std::wstring(sysDir) + L"\\vulkan-1.dll";
	HMODULE vk = LoadLibraryExW(vkPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!vk) {
		logger::warn("[Streamline] system vulkan-1.dll unavailable - assuming no DLSS-G hardware");
		return false;
	}

	bool found = false;
	auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
	auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(GetProcAddress(vk, "vkCreateInstance"));
	if (gipa && createInstance) {
		VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		ici.pApplicationInfo = &app;
		VkInstance instance = VK_NULL_HANDLE;
		if (createInstance(&ici, nullptr, &instance) == VK_SUCCESS && instance) {
			auto enumDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
			auto enumExts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance, "vkEnumerateDeviceExtensionProperties"));
			auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
			if (enumDevices && enumExts) {
				uint32_t count = 0;
				enumDevices(instance, &count, nullptr);
				std::vector<VkPhysicalDevice> devices(count);
				enumDevices(instance, &count, devices.data());
				for (auto dev : devices) {
					uint32_t extCount = 0;
					enumExts(dev, nullptr, &extCount, nullptr);
					std::vector<VkExtensionProperties> exts(extCount);
					enumExts(dev, nullptr, &extCount, exts.data());
					for (const auto& e : exts) {
						if (std::strcmp(e.extensionName, "VK_NV_optical_flow") == 0) {
							found = true;
							break;
						}
					}
					if (found)
						break;
				}
			}
			if (destroyInstance)
				destroyInstance(instance, nullptr);
		}
	}
	FreeLibrary(vk);
	logger::info("[Streamline] hardware probe: DLSS-G-class GPU (VK_NV_optical_flow) {}", found ? "present" : "absent");
	return found;
}

bool Streamline::Initialize()
{
	if (triedInit)
		return initialized;
	triedInit = true;

	const auto slDir = GetStreamlineDir();
	if (slDir.empty()) {
		logger::warn("[Streamline] could not resolve plugin directory");
		MarkUnavailable();
		return false;
	}

	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	if (!g_sl.interposer)
		g_sl.interposer = LoadLibraryExW(interposerPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!g_sl.interposer) {
		logger::info("[Streamline] sl.interposer.dll not present in '{}' — Streamline features disabled", slDir.string());
		MarkUnavailable();
		return false;
	}

	const bool resolved =
		Resolve(g_sl.slInit, "slInit") &&
		Resolve(g_sl.slIsFeatureSupported, "slIsFeatureSupported") &&
		Resolve(g_sl.slGetNewFrameToken, "slGetNewFrameToken") &&
		Resolve(g_sl.slSetTagForFrame, "slSetTagForFrame") &&
		Resolve(g_sl.slSetConstants, "slSetConstants") &&
		Resolve(g_sl.slEvaluateFeature, "slEvaluateFeature") &&
		Resolve(g_sl.slGetFeatureFunction, "slGetFeatureFunction");

	Resolve(g_sl.slSetFeatureLoaded, "slSetFeatureLoaded");
	if (!resolved) {
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
		MarkUnavailable();
		return false;
	}

	const auto slDirWide = slDir.wstring();
	const wchar_t* pluginPaths[] = { slDirWide.c_str() };
	// The controller keeps at most one frame-generation feature loaded at runtime.
	dlssgHardware = ProbeDLSSGHardware();

	std::vector<sl::Feature> featuresToLoad = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL,
		sl::kFeatureFSR, sl::kFeatureFSR_G, sl::kFeatureXeSS };
	if (dlssgHardware)
		featuresToLoad.push_back(sl::kFeatureDLSS_G);

	sl::Preferences pref{};
	pref.renderAPI = sl::RenderAPI::eVulkan;
	pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	pref.featuresToLoad = featuresToLoad.data();
	pref.numFeaturesToLoad = static_cast<uint32_t>(featuresToLoad.size());
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0";
	pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
	if (char v[2] = {}; GetEnvironmentVariableA("CS_SL_VERBOSE", v, sizeof(v)) && v[0] == 0x31)
		pref.logLevel = sl::LogLevel::eVerbose;
	else
		pref.logLevel = sl::LogLevel::eDefault;
	pref.logMessageCallback = &LogCallback;

	const sl::Result res = g_sl.slInit(pref, sl::kSDKVersion);
	if (res != sl::Result::eOk) {
		logger::warn("[Streamline] slInit failed (result {}) — Streamline features disabled", static_cast<int>(res));
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
		MarkUnavailable();
		return false;
	}

	initialized = true;
	logger::info("[Streamline] initialized on Vulkan (SDK {}.{}.{})",
		SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);
	return true;
}

void Streamline::SetVulkanDevice()
{
	if (!initialized || vulkanDeviceSet)
		return;

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk || !dxvk->IsAvailable()) {
		logger::warn("[Streamline] DXVK interop unavailable — cannot hand Vulkan device to SL");
		return;
	}

	vulkanDeviceSet = true;

	// Probe support against DXVK's physical device.
	sl::AdapterInfo adapter{};
	adapter.vkPhysicalDevice = dxvk->GetPhysicalDevice();
	const auto supported = [&](sl::Feature f) {
		const sl::Result r = g_sl.slIsFeatureSupported(f, adapter);
		if (r != sl::Result::eOk)
			logger::info("[Streamline] feature {} unsupported (result {})", f, static_cast<int>(r));
		return r == sl::Result::eOk;
	};

	featureDLSS = supported(sl::kFeatureDLSS);
	featureReflex = supported(sl::kFeatureReflex);
	featureDLSSG = supported(sl::kFeatureDLSS_G);
	featureXeSS = supported(sl::kFeatureXeSS);
	featureFSR = supported(sl::kFeatureFSR);
	featureFSRFG = supported(sl::kFeatureFSR_G);

	if (featureDLSS) {
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", reinterpret_cast<void*&>(g_sl.slDLSSGetOptimalSettings));
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSSetOptions));
		featureDLSS = g_sl.slDLSSSetOptions != nullptr;
	}
	if (featureReflex) {
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", reinterpret_cast<void*&>(g_sl.slReflexSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", reinterpret_cast<void*&>(g_sl.slReflexSleep));
		featureReflex = g_sl.slReflexSetOptions != nullptr && g_sl.slReflexSleep != nullptr;
	}
	g_sl.slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", reinterpret_cast<void*&>(g_sl.slPCLSetMarker));
	logger::info("[Streamline] PCL latency markers {}", g_sl.slPCLSetMarker ? "available" : "unavailable");
	if (featureDLSSG) {
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSGSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(g_sl.slDLSSGGetState));
		featureDLSSG = g_sl.slDLSSGSetOptions != nullptr && g_sl.slDLSSGGetState != nullptr;
	}
	if (featureFSR) {
		g_sl.slGetFeatureFunction(sl::kFeatureFSR, "slFSRSetOptions", reinterpret_cast<void*&>(g_sl.slFSRSetOptions));
		featureFSR = g_sl.slFSRSetOptions != nullptr;
	}
	if (featureFSRFG) {
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRGetFrameGenState", reinterpret_cast<void*&>(g_sl.slFSRGetFrameGenState));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationDiscardPreparedFrame", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationDiscardPreparedFrame));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationOwnsSwapchain", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationOwnsSwapchain));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationCompleteSwapchainTeardown", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationCompleteSwapchainTeardown));
		featureFSRFG = g_sl.slFSRFrameGenerationSetOptions != nullptr &&
			g_sl.slFSRFrameGenerationDiscardPreparedFrame != nullptr &&
			g_sl.slFSRFrameGenerationOwnsSwapchain != nullptr &&
			g_sl.slFSRFrameGenerationCompleteSwapchainTeardown != nullptr;
	}
	if (featureXeSS) {
		g_sl.slGetFeatureFunction(sl::kFeatureXeSS, "slXeSSSetOptions", reinterpret_cast<void*&>(g_sl.slXeSSSetOptions));
		featureXeSS = g_sl.slXeSSSetOptions != nullptr;
	}

	g_dxvk.Resolve();
	const bool frameGenerationInteropReady = g_dxvk.HasFrameGenerationInterop();
	// DXVK skips its frame-latency throttle unconditionally now: it never runs a present-wait
	// worker, so whatever owns the present loop owns the pacing.
	const bool dlssgInteropReady = frameGenerationInteropReady;
	featureDLSSG = featureDLSSG && dlssgHardware && dlssgInteropReady && dxvk->PresentWaitInteropReady();
	featureFSRFG = featureFSRFG && frameGenerationInteropReady &&
	               dxvk->FrameGenerationQueueInteropReady();

	if (featureDLSSG) {
		g_dlssgDesiredLoaded.store(true, std::memory_order_release);
		g_dlssgCurrentlyLoaded.store(true, std::memory_order_release);
	}
	if (featureFSRFG) {
		g_fsrfgDesiredLoaded.store(true, std::memory_order_release);
		g_fsrfgCurrentlyLoaded.store(true, std::memory_order_release);
	}

	logger::info("[Streamline] feature support: DLSS={} Reflex={} DLSS-G={} FSR={} FSR-G={} XeSS={} (FSR-FG fns {})",
		featureDLSS, featureReflex, featureDLSSG, featureFSR, featureFSRFG, featureXeSS,
		g_sl.slFSRFrameGenerationSetOptions && g_sl.slFSRFrameGenerationDiscardPreparedFrame &&
			g_sl.slFSRFrameGenerationOwnsSwapchain &&
			g_sl.slFSRFrameGenerationCompleteSwapchainTeardown ? "ok" : "missing");

	// Use Vulkan IDs because the D3D create hook may not see the adapter.
	if (auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
			dxvk->GetInstanceProcAddr()(dxvk->GetInstance(), "vkGetPhysicalDeviceProperties"))) {
		VkPhysicalDeviceProperties props{};
		getProps(dxvk->GetPhysicalDevice(), &props);
		isNvidiaGPU = props.vendorID == 0x10DE;
		isRTXBelow40Series = isNvidiaGPU &&
		                     ((props.deviceID >= 0x2200 && props.deviceID <= 0x2600) ||   // RTX 30 (Ampere)
								(props.deviceID >= 0x1E00 && props.deviceID <= 0x1FFF));   // RTX 20 (Turing w/ RT)
		logger::info("[Streamline] GPU vendor=0x{:04X} device=0x{:04X} -> DLSS preset group: {}",
			props.vendorID, props.deviceID,
			isNvidiaGPU ? (isRTXBelow40Series ? "RTX 20/30 (J)" : "RTX 40+ (M)") : "non-NVIDIA (default)");
	}
}

static sl::FrameToken* TokenForFrame(uint32_t a_frameId)
{
	sl::FrameToken* token = nullptr;
	if (g_sl.slGetNewFrameToken(token, &a_frameId) != sl::Result::eOk)
		return nullptr;
	return token;
}

static sl::FrameToken* RenderFrameToken()
{
	return TokenForFrame(g_sl.renderFrameId.load(std::memory_order_acquire));
}

static sl::Result cs_SetTagForFrame(sl::FrameToken& a_token, const sl::ViewportHandle& a_viewport,
	const sl::ResourceTag* a_tags, uint32_t a_tagCount, VkCommandBuffer a_commandBuffer)
{
	sl::Result result = sl::Result::eErrorExceptionHandler;
	__try {
		result = g_sl.slSetTagForFrame(a_token, a_viewport, a_tags, a_tagCount, a_commandBuffer);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
	return result;
}

static sl::Result cs_EvaluateFeature(sl::Feature a_feature, sl::FrameToken& a_token,
	const sl::ViewportHandle& a_viewport, VkCommandBuffer a_commandBuffer)
{
	sl::Result result = sl::Result::eErrorExceptionHandler;
	__try {
		const sl::BaseStructure* inputs[] = { &a_viewport };
		result = g_sl.slEvaluateFeature(
			a_feature, a_token, inputs, static_cast<uint32_t>(std::size(inputs)), a_commandBuffer);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
	return result;
}

static sl::Result cs_DiscardFSRFrameGenerationPreparedFrame(const sl::ViewportHandle& a_viewport)
{
	sl::Result result = sl::Result::eErrorNotInitialized;
	if (!g_sl.slFSRFrameGenerationDiscardPreparedFrame)
		return result;

	__try {
		result = g_sl.slFSRFrameGenerationDiscardPreparedFrame(a_viewport);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		result = sl::Result::eErrorExceptionHandler;
	}
	return result;
}

// The frame index the next rendered frame will use. slReflexSleep, SimulationStart, the render-thread
// PCL markers, tags and constants must all agree on one index per frame, or Reflex never sees a frame
// close and its frame limiter falls back to open-loop sleeping.
//
// This deliberately reads Streamline's own counter rather than the game's. frameCount advances at
// Present, so during input polling and rendering it still holds the previous frame's value, and
// deriving the two sides from separate counters let them drift: when a render frame ran without an
// intervening input poll the latch went stale, BeginRenderFrame took its strictly-increasing +1
// branch, and nothing pulled the simulation side back again -- measured as a permanent off-by-one on
// 92-98% of frames. Reading the render counter here means the two cannot diverge: whatever this
// returns is what the next BeginRenderFrame adopts, and a skipped poll resynchronises on the frame
// after it.
static uint32_t SimFrameId()
{
	return g_sl.renderFrameId.load(std::memory_order_acquire) + 1u;
}

void Streamline::BeginRenderFrame()
{
	if (g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire) &&
		!g_sl.dispatchFaulted.load(std::memory_order_acquire))
		(void)DiscardFSRFrameGenerationPreparedFrame();
	// Adopt the index SimulationStart already used. It is SimFrameId(), i.e. renderFrameId + 1, so
	// this is strictly increasing by construction -- which the tag and constant keying and CS's own
	// once-per-frame guards (s_evalFrameByVp / s_constFrameByVp) require: a repeated index reads as
	// "already evaluated" and silently dropped every second FSR-FG evaluation, measured 11-12
	// evaluations per second against 20-21 rendered frames with output ~24% under target. Advancing
	// it here rather than re-deriving it from the game counter also keeps sl.dlss_g from querying a
	// frame Streamline has no record of at present time:
	//   commonInterface.h:622[get] Unable to find 'common' constants for frame
	//   commonInterface.h:256[getTaggedResource] Failed to find global tag 'kBufferTypeDepth'
	// which interpolates without depth or motion vectors and shows up as an occasional generated
	// frame collapsing towards a corner.
	const uint32_t latched = g_sl.simMarkerFrameId.load(std::memory_order_acquire);
	const uint32_t next = g_sl.renderFrameId.load(std::memory_order_acquire) + 1u;
	g_sl.renderFrameId.store(latched == next ? latched : next, std::memory_order_release);
	g_sl.dlssgTaggedThisFrame = false;
}

bool Streamline::DiscardFSRFrameGenerationPreparedFrame()
{
	if (!g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire))
		return true;
	if (!g_sl.slFSRFrameGenerationDiscardPreparedFrame ||
		g_sl.dispatchFaulted.load(std::memory_order_acquire))
		return false;

	const sl::ViewportHandle fgViewport{ 1 };
	const sl::Result result = cs_DiscardFSRFrameGenerationPreparedFrame(fgViewport);
	if (result == sl::Result::eErrorInvalidState) {
		return true;
	}
	if (result != sl::Result::eOk) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR prepared-frame discard failed (result {})",
			static_cast<int>(result));
		return false;
	}


	return true;
}

void Streamline::CaptureDLSSGPresentState()
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted ||
		!g_dlssgCurrentlyLoaded.load(std::memory_order_acquire) || !g_sl.dlssgModeOn)
		return;
	__try {
		sl::DLSSGState state{};
		if (g_sl.slDLSSGGetState(g_sl.viewport, state, nullptr) == sl::Result::eOk) {
			// state.status says why DLSS-G is not generating. Report each distinct value once:
			// without it a non-functional DLSS-G is silent and simply halves the frame rate.
			static uint32_t s_sampleTick = 0u;
			if ((++s_sampleTick % 600u) == 0u) {
				logger::info("[Streamline] DLSS-G presented {} frame(s) since last query (status 0x{:X}, max {}, vsyncSupportAvailable={})",
					state.numFramesActuallyPresented, static_cast<uint32_t>(state.status), state.numFramesToGenerateMax,
					state.bIsVsyncSupportAvailable == sl::Boolean::eTrue ? "yes" :
					state.bIsVsyncSupportAvailable == sl::Boolean::eFalse ? "no" : "invalid");
			}
			static uint32_t s_lastStatus = UINT32_MAX;
			const uint32_t status = static_cast<uint32_t>(state.status);
			if (status != s_lastStatus) {
				s_lastStatus = status;
				if (status == 0u) {
					logger::info("[Streamline] DLSS-G status OK (presenting {} frames, max {})",
						state.numFramesActuallyPresented, state.numFramesToGenerateMax);
				} else {
					logger::warn("[Streamline] DLSS-G NOT generating - status 0x{:X}{}{}{}{}{} (presenting {})",
						status,
						(status & 0x1) ? " resolutionTooLow" : "",
						(status & 0x2) ? " reflexNotDetectedAtRuntime" : "",
						(status & 0x4) ? " hdrFormatNotSupported" : "",
						(status & 0x8) ? " commonConstantsInvalid" : "",
						(status & 0x10) ? " getCurrentBackBufferIndexNotCalled" : "",
						state.numFramesActuallyPresented);
				}
			}
			g_sl.frameGenerationMultiplier.store(
				std::max(state.numFramesActuallyPresented, 1u), std::memory_order_release);
			g_sl.dlssgCloneTagsPrimed.store(true, std::memory_order_release);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::UpdateReflex(bool a_enable, bool a_boost, uint32_t a_frameLimitUs)
{
	g_sl.latencyMarkersNeeded.store(a_enable || g_dlssgCurrentlyLoaded.load(std::memory_order_acquire) ||
			g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire),
		std::memory_order_release);
	if (!initialized || !featureReflex || g_sl.dispatchFaulted)
		return;

	const sl::ReflexMode mode = !a_enable ? sl::ReflexMode::eOff :
	                            a_boost   ? sl::ReflexMode::eLowLatencyWithBoost :
	                                        sl::ReflexMode::eLowLatency;

	__try {
		if (!g_sl.reflexCacheValid || g_sl.reflexCachedMode != mode || g_sl.reflexCachedFrameLimitUs != a_frameLimitUs) {
			sl::ReflexOptions options{};
			options.mode = mode;
			options.frameLimitUs = a_frameLimitUs;
			const sl::Result reflexRes = g_sl.slReflexSetOptions(options);
			if (reflexRes == sl::Result::eOk) {
				g_sl.reflexCachedMode = mode;
				g_sl.reflexCachedFrameLimitUs = a_frameLimitUs;
				g_sl.reflexCacheValid = true;
				logger::info("[Streamline] Reflex mode={} frameLimitUs={}",
					mode == sl::ReflexMode::eOff ? "off" :
					mode == sl::ReflexMode::eLowLatencyWithBoost ? "low-latency+boost" : "low-latency",
					a_frameLimitUs);
			} else {
				logger::warn("[Streamline] slReflexSetOptions failed (result {})", static_cast<int>(reflexRes));
			}
		}
		if (mode != sl::ReflexMode::eOff) {
			// PollInputDevices can run more than once per rendered frame.
			static uint32_t s_lastSleepFrame = UINT32_MAX;
			const uint32_t simFrame = SimFrameId();
			if (s_lastSleepFrame != simFrame) {
				s_lastSleepFrame = simFrame;
				if (sl::FrameToken* token = TokenForFrame(simFrame))
					g_sl.slReflexSleep(*token);
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] Reflex dispatch faulted — Streamline disabled for this session");
	}
}

void Streamline::SetPCLMarker(PclMarker a_marker)
{
	if (!initialized || !g_sl.slPCLSetMarker || g_sl.dispatchFaulted)
		return;
	if (!g_sl.latencyMarkersNeeded.load(std::memory_order_acquire))
		return;

	// Emit SimulationStart once per simulated frame, not once per input poll.
	uint32_t simFrame = 0;
	if (a_marker == PclMarker::SimulationStart) {
		static uint32_t s_lastSimFrame = UINT32_MAX;
		simFrame = SimFrameId();
		if (s_lastSimFrame == simFrame)
			return;
		s_lastSimFrame = simFrame;
		g_sl.simMarkerFrameId.store(simFrame, std::memory_order_release);
	}

	__try {
		const bool renderThreadMarker =
			a_marker == PclMarker::RenderSubmitStart || a_marker == PclMarker::RenderSubmitEnd ||
			a_marker == PclMarker::PresentStart || a_marker == PclMarker::PresentEnd ||
			a_marker == PclMarker::TriggerFlash ||
			a_marker == PclMarker::SimulationEnd;
		sl::FrameToken* token = renderThreadMarker ?
		                            RenderFrameToken() :
		                            TokenForFrame(simFrame ? simFrame : SimFrameId());
		if (token)
			g_sl.slPCLSetMarker(static_cast<sl::PCLMarker>(a_marker), *token);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] PCL marker faulted — Streamline disabled for this session");
	}
}

// Returns false until the engine camera matrices are finite and invertible.
static bool cs_IsFiniteMatrix(const Matrix& a_matrix)
{
	return std::isfinite(a_matrix._11) && std::isfinite(a_matrix._12) &&
	       std::isfinite(a_matrix._13) && std::isfinite(a_matrix._14) &&
	       std::isfinite(a_matrix._21) && std::isfinite(a_matrix._22) &&
	       std::isfinite(a_matrix._23) && std::isfinite(a_matrix._24) &&
	       std::isfinite(a_matrix._31) && std::isfinite(a_matrix._32) &&
	       std::isfinite(a_matrix._33) && std::isfinite(a_matrix._34) &&
	       std::isfinite(a_matrix._41) && std::isfinite(a_matrix._42) &&
	       std::isfinite(a_matrix._43) && std::isfinite(a_matrix._44);
}

static bool cs_BuildConstants(sl::Constants& a_consts, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	a_consts = {};
	a_consts.cameraAspectRatio = static_cast<float>(a_outputWidth) / static_cast<float>(a_outputHeight);
	a_consts.cameraFOV = Util::GetVerticalFOVRad();
	a_consts.cameraNear = *globals::game::cameraNear;
	a_consts.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

	a_consts.cameraMotionIncluded = sl::Boolean::eTrue;
	a_consts.cameraPinholeOffset = { 0.f, 0.f };
	a_consts.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	a_consts.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	a_consts.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	const auto cameraPosAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();
	a_consts.cameraPos = *reinterpret_cast<const sl::float3*>(&cameraPosAdjust);
	a_consts.cameraViewToClip = *reinterpret_cast<const sl::float4x4*>(&cameraViewToClip);
	a_consts.depthInverted = sl::Boolean::eFalse;

	sl::recalculateCameraMatrices(a_consts);

	// Translate between the current and previous camera-relative origins before reprojection.
	// Streamline applies jitter separately, so both matrices remain unjittered.
	Matrix curVP = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	Matrix prevVP = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered().Transpose();
	const auto& posAdj = globals::game::frameBufferCached.GetCameraPosAdjust();
	const auto& prevPosAdj = globals::game::frameBufferCached.GetCameraPreviousPosAdjust();
	Matrix camDelta = Matrix::CreateTranslation(posAdj.x - prevPosAdj.x, posAdj.y - prevPosAdj.y, posAdj.z - prevPosAdj.z);
	a_consts.reset = sl::Boolean::eFalse;
	Matrix clipToPrevClip = curVP.Invert() * camDelta * prevVP;
	Matrix prevClipToClip = clipToPrevClip.Invert();

	// Matrix::Invert() yields NaN/inf when the source is singular, which the cached view-projection
	// is on transient frames -- a camera cut, a menu, the frame after a load. Previously any such
	// frame failed the finite check below and skipped the whole evaluate, which starves frame
	// generation of camera constants for that frame and makes it reproject from stale data. That is
	// visible as an interpolated frame collapsing towards a corner.
	//
	// Substitute identity, which says "no camera motion this frame", and flag a temporal reset so
	// Streamline discards history rather than reprojecting through it. Feeding valid constants every
	// frame matters more than describing the motion of a frame whose matrices do not exist yet.
	const bool reprojectionFinite = cs_IsFiniteMatrix(clipToPrevClip) && cs_IsFiniteMatrix(prevClipToClip);
	if (!reprojectionFinite) {
		clipToPrevClip = Matrix::Identity;
		prevClipToClip = Matrix::Identity;
		a_consts.reset = sl::Boolean::eTrue;
		static bool s_loggedSingular = false;
		if (!s_loggedSingular) {
			s_loggedSingular = true;
			logger::warn("[Streamline] singular camera reprojection matrices - substituting identity "
			             "and resetting temporal history for affected frames");
		}
	}
	a_consts.clipToPrevClip = *reinterpret_cast<const sl::float4x4*>(&clipToPrevClip);
	a_consts.prevClipToClip = *reinterpret_cast<const sl::float4x4*>(&prevClipToClip);

	a_consts.jitterOffset = { -a_jitterX, -a_jitterY };
	// Reset temporal history after leaving a loading screen.
	{
		static bool s_wasLoading = false;
		static uint32_t s_observedFrame = UINT32_MAX;
		static uint32_t s_resetFrame = UINT32_MAX;
		const bool loading = globals::state->isLoadingMenuOpen;
		if (s_observedFrame != g_sl.renderFrameId.load(std::memory_order_acquire)) {
			s_observedFrame = g_sl.renderFrameId.load(std::memory_order_acquire);
			if (!loading && s_wasLoading)
				s_resetFrame = g_sl.renderFrameId.load(std::memory_order_acquire);
			s_wasLoading = loading;
		}
		// Preserve a reset already requested above (singular reprojection matrices).
		if (s_resetFrame == g_sl.renderFrameId.load(std::memory_order_acquire))
			a_consts.reset = sl::Boolean::eTrue;
	}
	a_consts.mvecScale = { 1.0f, 1.0f };
	a_consts.motionVectors3D = sl::Boolean::eFalse;
	a_consts.motionVectorsInvalidValue = FLT_MIN;
	a_consts.orthographicProjection = sl::Boolean::eFalse;
	a_consts.motionVectorsDilated = sl::Boolean::eFalse;
	a_consts.motionVectorsJittered = sl::Boolean::eFalse;

	// Reject singular engine matrices before passing constants to Streamline.
	const bool matricesFinite = cs_IsFiniteMatrix(cameraViewToClip) &&
	                            cs_IsFiniteMatrix(clipToPrevClip) &&
	                            cs_IsFiniteMatrix(prevClipToClip);
	const bool basisFinite = std::isfinite(a_consts.cameraRight.x) &&
	                         std::isfinite(a_consts.cameraRight.y) &&
	                         std::isfinite(a_consts.cameraRight.z) &&
	                         std::isfinite(a_consts.cameraUp.x) &&
	                         std::isfinite(a_consts.cameraUp.y) &&
	                         std::isfinite(a_consts.cameraUp.z) &&
	                         std::isfinite(a_consts.cameraFwd.x) &&
	                         std::isfinite(a_consts.cameraFwd.y) &&
	                         std::isfinite(a_consts.cameraFwd.z);
	const bool scalarsFinite = std::isfinite(a_consts.cameraAspectRatio) &&
	                           std::isfinite(a_consts.cameraFOV) &&
	                           std::isfinite(a_consts.cameraNear) &&
	                           std::isfinite(a_consts.cameraFar);
	static bool s_cameraDataInvalid = false;
	if (!matricesFinite || !basisFinite || !scalarsFinite) {
		if (!s_cameraDataInvalid) {
			s_cameraDataInvalid = true;
			logger::warn("[Streamline] skipping evaluate: invalid camera constants "
				"(matrices={} basis={} scalars={} proj=[{},{},{},{},{},{}])",
				matricesFinite, basisFinite, scalarsFinite,
				cameraViewToClip._11, cameraViewToClip._22, cameraViewToClip._33,
				cameraViewToClip._34, cameraViewToClip._43, cameraViewToClip._44);
		}
		return false;
	}
	if (s_cameraDataInvalid) {
		s_cameraDataInvalid = false;
		logger::info("[Streamline] camera constants became valid; evaluations resumed");
	}

	return true;
}

static VkImageAspectFlags cs_ImageAspect(VkFormat a_format)
{
	switch (a_format) {
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

struct cs_VulkanResultAttempt
{
	VkResult result = VK_ERROR_DEVICE_LOST;
	DWORD exceptionCode = 0;
};

struct cs_VulkanVoidAttempt
{
	DWORD exceptionCode = 0;
	bool completed = false;
};

struct cs_VulkanProcAttempt
{
	PFN_vkVoidFunction function = nullptr;
	DWORD exceptionCode = 0;
};

struct cs_GetVkImageAttempt
{
	bool succeeded = false;
	DWORD exceptionCode = 0;
};

static cs_GetVkImageAttempt cs_GetVkImageSEH(DXVKInterop* a_dxvk, ID3D11Resource* a_resource,
	VkImage* a_image, VkImageLayout* a_layout, VkImageCreateInfo* a_info) noexcept
{
	cs_GetVkImageAttempt attempt{};
	__try {
		attempt.succeeded = a_dxvk &&
			a_dxvk->GetVkImage(a_resource, a_image, a_layout, a_info);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanProcAttempt cs_GetDeviceProcAddrSEH(
	PFN_vkGetDeviceProcAddr a_getProcAddr, VkDevice a_device, const char* a_name) noexcept
{
	cs_VulkanProcAttempt attempt{};
	__try {
		if (a_getProcAddr)
			attempt.function = a_getProcAddr(a_device, a_name);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanResultAttempt cs_CreateImageViewSEH(PFN_vkCreateImageView a_createImageView,
	VkDevice a_device, const VkImageViewCreateInfo* a_createInfo, VkImageView* a_view) noexcept
{
	cs_VulkanResultAttempt attempt{};
	__try {
		if (a_createImageView)
			attempt.result = a_createImageView(a_device, a_createInfo, nullptr, a_view);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanVoidAttempt cs_DestroyImageViewSEH(PFN_vkDestroyImageView a_destroyImageView,
	VkDevice a_device, VkImageView a_view) noexcept
{
	cs_VulkanVoidAttempt attempt{};
	__try {
		if (a_destroyImageView && a_view != VK_NULL_HANDLE)
			a_destroyImageView(a_device, a_view, nullptr);
		attempt.completed = true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanVoidAttempt cs_PipelineBarrierSEH(VkCommandBuffer a_commandBuffer,
	const VkImageMemoryBarrier* a_barrier) noexcept
{
	cs_VulkanVoidAttempt attempt{};
	__try {
		vkCmdPipelineBarrier(a_commandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
			0, nullptr, 0, nullptr, 1, a_barrier);
		attempt.completed = true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

// Streamline's Vulkan backend requires a matching VkImageView for every resource.
static bool cs_WrapInteropImage(DXVKInterop* a_dxvk, VkDevice a_device, PFN_vkCreateImageView a_createView,
	ID3D11Resource* a_res, sl::Resource& a_out, sl::SubresourceRange& a_subresource,
	VkImageView& a_outView, bool& a_terminalFault)
{
	a_outView = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	const cs_GetVkImageAttempt imageAttempt =
		cs_GetVkImageSEH(a_dxvk, a_res, &image, &layout, &info);
	if (imageAttempt.exceptionCode) {
		a_terminalFault = true;
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DXVK image interop faulted (SEH {:#x})",
			imageAttempt.exceptionCode);
		return false;
	}
	if (!imageAttempt.succeeded || image == VK_NULL_HANDLE)
		return false;
	VkImageView view = VK_NULL_HANDLE;
	if (!a_createView)
		return false;
	VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	ci.image = image;
	ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ci.format = info.format;
	ci.subresourceRange.aspectMask = cs_ImageAspect(info.format);
	ci.subresourceRange.levelCount = 1;
	ci.subresourceRange.layerCount = 1;
	const cs_VulkanResultAttempt createAttempt =
		cs_CreateImageViewSEH(a_createView, a_device, &ci, &view);
	if (createAttempt.exceptionCode || createAttempt.result != VK_SUCCESS || view == VK_NULL_HANDLE) {
		if (createAttempt.exceptionCode || view != VK_NULL_HANDLE) {
			a_terminalFault = true;
		}
		view = VK_NULL_HANDLE;
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] Vulkan image-view creation failed (result {}, SEH {:#x})",
			static_cast<int>(createAttempt.result), createAttempt.exceptionCode);
		a_outView = VK_NULL_HANDLE;
		return false;
	}
	a_outView = view;
	a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, view, static_cast<uint32_t>(layout) };
	a_out.width = info.extent.width;
	a_out.height = info.extent.height;
	a_out.nativeFormat = static_cast<uint32_t>(info.format);
	a_out.mipLevels = info.mipLevels;
	a_out.arrayLayers = info.arrayLayers;
	a_out.usage = static_cast<uint32_t>(info.usage);
	a_out.flags = static_cast<uint32_t>(info.flags);
	a_subresource.aspectMask = ci.subresourceRange.aspectMask;
	a_subresource.baseMipLevel = 0;
	a_subresource.levelCount = 1;
	a_subresource.baseArrayLayer = 0;
	a_subresource.layerCount = 1;
	a_out.next = &a_subresource;
	return true;
}

// a_dxvk/a_resources/a_resourceCount only ever fed the removed quarantine path; they are kept
// so the call sites still read as "these views belong to these resources".
static bool cs_DestroyViews([[maybe_unused]] DXVKInterop* a_dxvk, VkDevice a_device,
	PFN_vkDestroyImageView a_destroyImageView, VkImageView* a_views, uint32_t a_count,
	[[maybe_unused]] ID3D11Resource* const* a_resources = nullptr,
	[[maybe_unused]] uint32_t a_resourceCount = 0)
{
	if (!a_destroyImageView)
		return false;
	for (uint32_t i = 0; i < a_count; ++i) {
		if (a_views[i] == VK_NULL_HANDLE)
			continue;
		const cs_VulkanVoidAttempt destroyAttempt =
			cs_DestroyImageViewSEH(a_destroyImageView, a_device, a_views[i]);
		if (!destroyAttempt.completed) {
			a_views[i] = VK_NULL_HANDLE;
			g_sl.dispatchFaulted = true;
			return false;
		}
		a_views[i] = VK_NULL_HANDLE;
	}
	return true;
}

static bool cs_BarrierUpscalerOutput(VkCommandBuffer a_commandBuffer, const sl::Resource& a_output)
{
	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.oldLayout = static_cast<VkImageLayout>(a_output.state);
	barrier.newLayout = static_cast<VkImageLayout>(a_output.state);
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = static_cast<VkImage>(a_output.native);
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = a_output.mipLevels;
	barrier.subresourceRange.layerCount = a_output.arrayLayers;
	const cs_VulkanVoidAttempt barrierAttempt = cs_PipelineBarrierSEH(a_commandBuffer, &barrier);
	if (!barrierAttempt.completed)
		g_sl.dispatchFaulted = true;
	return barrierAttempt.completed;
}

// Whether a DLSS-G tag submission should carry a present-wait signal. Only DLSS-G presents consume
// one, and only on a swapchain the presenter has not already shown it will leave unattached.
static bool cs_WantPresentWaitSignal(const DXVKInterop* a_dxvk)
{
	return g_dlssgCurrentlyLoaded.load(std::memory_order_acquire) &&
	       !a_dxvk->IsPresentWaitUnattachedForSwapchain();
}

static bool cs_SubmitPresentTags(DXVKInterop* a_dxvk, sl::FrameToken& a_token,
	const sl::ViewportHandle& a_viewport, const sl::ResourceTag* a_tags, uint32_t a_tagCount,
	const VkImageView* a_views, uint32_t a_viewCount,
	ID3D11Resource* const* a_resources, uint32_t a_resourceCount, sl::Result& a_tagResult,
	bool& a_lifetimesRetained, bool a_signalForPresent)
{
	a_lifetimesRetained = false;
	auto transaction = a_dxvk->BeginFrameCommandBuffer();
	if (!transaction) {
		logger::error("[Streamline] present tags: could not acquire an interop command buffer");
		return false;
	}

	a_tagResult = cs_SetTagForFrame(
		a_token, a_viewport, a_tags, a_tagCount, transaction.GetCommandBuffer());
	if (a_tagResult != sl::Result::eOk) {
		logger::error("[Streamline] present tags: slSetTagForFrame failed (result {})",
			static_cast<int>(a_tagResult));
		return false;
	}
	// Always request the present-wait signal: these tags are only ever submitted on the
	// DLSS-G path, and the presenter attaches the semaphore whenever the ownership query
	// reports a DLSS-G-owned swapchain. Gating this on "already loaded" deadlocks the
	// first present of a transition, because loading cannot complete until presents do.
	if (!a_dxvk->SubmitFrameCommandBuffer(transaction, a_signalForPresent)) {
		// The tags themselves succeeded; the submission is what failed. Reporting a_tagResult
		// here previously produced the nonsensical "tag submission failed (result 0)".
		logger::error("[Streamline] present tags: interop submit failed (signalForPresent={})",
			a_signalForPresent);
		if (transaction.SubmissionMayBeInFlight()) {
			a_dxvk->QueueViewsForDeferredDelete(transaction, a_views, a_viewCount);
			a_dxvk->QueueResourcesForDeferredRelease(transaction, a_resources, a_resourceCount);
			a_lifetimesRetained = true;
		}
		return false;
	}

	a_dxvk->QueueViewsForDeferredDelete(transaction, a_views, a_viewCount);
	a_dxvk->QueueResourcesForDeferredRelease(transaction, a_resources, a_resourceCount);
	return true;
}

static bool cs_CanReleaseFailedFSRFrame(DXVKInterop* a_dxvk,
	DXVKInterop::CommandTransaction& a_transaction, const sl::ViewportHandle& a_viewport,
	const VkImageView* a_views, uint32_t a_viewCount,
	ID3D11Resource* const* a_resources, uint32_t a_resourceCount)
{
	if (!a_transaction.SubmissionMayBeInFlight() &&
		!g_sl.dispatchFaulted.load(std::memory_order_acquire)) {
		const sl::Result discardResult = cs_DiscardFSRFrameGenerationPreparedFrame(a_viewport);
		if (discardResult == sl::Result::eOk || discardResult == sl::Result::eErrorInvalidState)
			return true;
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] failed to discard a partially accepted FSR-FG frame (result {})",
			static_cast<int>(discardResult));
	}

	a_dxvk->QueueResourcesForDeferredRelease(a_transaction, a_resources, a_resourceCount);
	a_dxvk->QueueViewsForDeferredDelete(a_transaction, a_views, a_viewCount);
	return false;
}

static sl::Result cs_EvaluateFeatureCore(sl::Feature a_feature, const sl::ViewportHandle& a_viewport,
	ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY, ID3D11Resource* a_hudlessColor = nullptr,
	bool* a_outputReady = nullptr, bool* a_skipped = nullptr)
{
	if (a_outputReady)
		*a_outputReady = false;
	if (a_skipped)
		*a_skipped = false;

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk)
		return sl::Result::eErrorNotInitialized;

	sl::FrameToken* token = RenderFrameToken();
	if (!token) {
		if (a_skipped)
			*a_skipped = true;
		return sl::Result::eOk;
	}

	// Pair constants and evaluation once per frame and viewport.
	static uint32_t s_evalFrameByVp[2] = { UINT32_MAX, UINT32_MAX };
	static uint32_t s_constFrameByVp[2] = { UINT32_MAX, UINT32_MAX };
	const uint32_t vpId = a_viewport;
	if (vpId < 2) {
		if (s_evalFrameByVp[vpId] == g_sl.renderFrameId.load(std::memory_order_acquire)) {
			if (a_skipped)
				*a_skipped = true;
			return sl::Result::eOk;
		}
		if (s_constFrameByVp[vpId] != g_sl.renderFrameId.load(std::memory_order_acquire)) {
			sl::Constants consts;
			if (!cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY)) {
				if (a_skipped)
					*a_skipped = true;
				return sl::Result::eOk;
			}
			const sl::Result constantsRes = g_sl.slSetConstants(consts, *token, a_viewport);
			if (constantsRes != sl::Result::eOk) {
				logger::error("[Streamline] slSetConstants failed for viewport {} (result {})",
					vpId, static_cast<int>(constantsRes));
				return constantsRes;
			}
			s_constFrameByVp[vpId] = g_sl.renderFrameId.load(std::memory_order_acquire);
		}
	} else {
		sl::Constants consts;
		if (!cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY)) {
			if (a_skipped)
				*a_skipped = true;
			return sl::Result::eOk;
		}
		const sl::Result constantsRes = g_sl.slSetConstants(consts, *token, a_viewport);
		if (constantsRes != sl::Result::eOk)
			return constantsRes;
	}

	VkDevice vkDevice = dxvk->GetDevice();
	const cs_VulkanProcAttempt createProcAttempt = cs_GetDeviceProcAddrSEH(
		dxvk->GetDeviceProcAddr(), vkDevice, "vkCreateImageView");
	const cs_VulkanProcAttempt destroyProcAttempt = cs_GetDeviceProcAddrSEH(
		dxvk->GetDeviceProcAddr(), vkDevice, "vkDestroyImageView");
	if (createProcAttempt.exceptionCode || destroyProcAttempt.exceptionCode) {
		g_sl.dispatchFaulted = true;
		return sl::Result::eErrorExceptionHandler;
	}
	auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(createProcAttempt.function);
	auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(destroyProcAttempt.function);
	if (!vkCreateImageView || !vkDestroyImageView)
		return sl::Result::eErrorNotInitialized;
	ID3D11Resource* resources[] = {
		a_colorIn, a_colorOut, a_depth, a_motionVectors, a_hudlessColor
	};
	VkImageView views[5] = {};
	sl::SubresourceRange subresources[5]{};
	int nv = 0;
	int nr = 0;
	bool viewCreationTerminalFault = false;
	const auto wrap = [&](ID3D11Resource* a_res, sl::Resource& a_out) -> bool {
		VkImageView v = VK_NULL_HANDLE;
		if (nr >= static_cast<int>(std::size(subresources)))
			return false;
		const bool wrapped = cs_WrapInteropImage(
			dxvk, vkDevice, vkCreateImageView, a_res, a_out, subresources[nr], v,
			viewCreationTerminalFault);
		if (v != VK_NULL_HANDLE && nv < static_cast<int>(std::size(views)))
			views[nv++] = v;
		if (!wrapped)
			return false;
		++nr;
		return true;
	};

	const bool haveColor = (a_colorIn && a_colorOut);
	const bool haveHudless = (a_hudlessColor != nullptr);
	sl::Resource colorInRes{}, colorOutRes{}, depthRes{}, mvecRes{}, hudlessRes{};
	bool ok = wrap(a_depth, depthRes) &&
	          wrap(a_motionVectors, mvecRes);
	if (ok && haveColor)
		ok = wrap(a_colorIn, colorInRes) &&
		     wrap(a_colorOut, colorOutRes);
	if (ok && haveHudless)
		ok = wrap(a_hudlessColor, hudlessRes);
	if (!ok) {
		if (viewCreationTerminalFault) {
			// The destroy entry point already faulted on these handles; calling it again would fault
			// again, so abandon the views rather than retrying the destroy.
			std::fill(std::begin(views), std::end(views), VK_NULL_HANDLE);
		} else {
			cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views, static_cast<uint32_t>(nv),
				resources, static_cast<uint32_t>(std::size(resources)));
		}
		return sl::Result::eErrorMissingInputParameter;
	}

	sl::Extent renderExtent{ 0, 0, a_renderWidth, a_renderHeight };
	sl::Extent outputExtent{ 0, 0, a_outputWidth, a_outputHeight };
	sl::ResourceTag tags[5];
	uint32_t nt = 0;
	if (haveColor) {
		tags[nt++] = sl::ResourceTag{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent };
		tags[nt++] = sl::ResourceTag{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent };
	}
	const auto inputLifecycle = haveColor ?
		sl::ResourceLifecycle::eValidUntilEvaluate : sl::ResourceLifecycle::eOnlyValidNow;
	tags[nt++] = sl::ResourceTag{ &depthRes, sl::kBufferTypeDepth, inputLifecycle, &renderExtent };
	tags[nt++] = sl::ResourceTag{ &mvecRes, sl::kBufferTypeMotionVectors, inputLifecycle, &renderExtent };
	if (haveHudless)
		tags[nt++] = sl::ResourceTag{ &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent };

	sl::Result evalRes = sl::Result::eErrorNotInitialized;
	auto transaction = dxvk->BeginFrameCommandBuffer();
	if (transaction) {
		const VkCommandBuffer cmd = transaction.GetCommandBuffer();
		const sl::Result tagRes = cs_SetTagForFrame(*token, a_viewport, tags, nt, cmd);
		if (tagRes != sl::Result::eOk) {
			logger::error("[Streamline] slSetTagForFrame failed for feature {} viewport {} (result {})",
				static_cast<uint32_t>(a_feature), vpId, static_cast<int>(tagRes));
			const bool canRelease = a_feature != sl::kFeatureFSR_G ||
				cs_CanReleaseFailedFSRFrame(dxvk, transaction, a_viewport,
					views, static_cast<uint32_t>(nv), resources, static_cast<uint32_t>(std::size(resources)));
			if (canRelease)
				cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views,
					static_cast<uint32_t>(nv), resources,
					static_cast<uint32_t>(std::size(resources)));
			return tagRes;
		}

		evalRes = cs_EvaluateFeature(a_feature, *token, a_viewport, cmd);
		if (evalRes != sl::Result::eOk) {
			const bool canRelease = a_feature != sl::kFeatureFSR_G ||
				cs_CanReleaseFailedFSRFrame(dxvk, transaction, a_viewport,
					views, static_cast<uint32_t>(nv), resources, static_cast<uint32_t>(std::size(resources)));
			if (canRelease)
				cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views,
					static_cast<uint32_t>(nv), resources,
					static_cast<uint32_t>(std::size(resources)));
			return evalRes;
		}

		if (haveColor && !cs_BarrierUpscalerOutput(cmd, colorOutRes)) {
			// The destroy entry point already faulted on these handles; calling it again would fault
			// again, so abandon the views rather than retrying the destroy.
			std::fill(std::begin(views), std::end(views), VK_NULL_HANDLE);
			return sl::Result::eErrorExceptionHandler;
		}

		if (dxvk->SubmitFrameCommandBuffer(transaction)) {
			dxvk->QueueResourcesForDeferredRelease(
				transaction, resources, static_cast<uint32_t>(std::size(resources)));
			dxvk->QueueViewsForDeferredDelete(transaction, views, static_cast<uint32_t>(nv));
			if (a_outputReady)
				*a_outputReady = true;
			if (vpId < 2)
				s_evalFrameByVp[vpId] = g_sl.renderFrameId.load(std::memory_order_acquire);
		} else {
			if (a_feature == sl::kFeatureFSR_G) {
				const bool canRelease = cs_CanReleaseFailedFSRFrame(dxvk, transaction, a_viewport,
					views, static_cast<uint32_t>(nv), resources, static_cast<uint32_t>(std::size(resources)));
				if (canRelease)
					cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views,
						static_cast<uint32_t>(nv), resources,
						static_cast<uint32_t>(std::size(resources)));
			} else if (transaction.SubmissionMayBeInFlight()) {
				dxvk->QueueResourcesForDeferredRelease(
					transaction, resources, static_cast<uint32_t>(std::size(resources)));
				dxvk->QueueViewsForDeferredDelete(transaction, views, static_cast<uint32_t>(nv));
			} else {
				cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views,
					static_cast<uint32_t>(nv), resources,
					static_cast<uint32_t>(std::size(resources)));
			}
			evalRes = sl::Result::eErrorExceptionHandler;
		}
	} else {
		cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views,
			static_cast<uint32_t>(nv), resources,
			static_cast<uint32_t>(std::size(resources)));
		if (a_skipped)
			*a_skipped = true;
		evalRes = sl::Result::eOk;
	}
	return evalRes;
}

static Streamline::EvaluationResult cs_ClassifyEvaluation(
	sl::Result a_result, bool a_outputReady, bool a_skipped)
{
	if (a_result != sl::Result::eOk)
		return Streamline::EvaluationResult::kFailed;
	if (a_outputReady)
		return Streamline::EvaluationResult::kReady;
	return a_skipped ? Streamline::EvaluationResult::kSkipped : Streamline::EvaluationResult::kFailed;
}

Streamline::EvaluationResult Streamline::EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode,
	float a_jitterX, float a_jitterY)
{
	bool outputReady = false;
	bool evaluationSkipped = false;
	EvaluationResult result = EvaluationResult::kFailed;
	if (!initialized || !featureDLSS || g_sl.dispatchFaulted)
		return result;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return result;

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return result;

	__try {
		sl::DLSSMode dlssMode = sl::DLSSMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			dlssMode = sl::DLSSMode::eDLAA;
			break;
		case 1:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		case 2:
			dlssMode = sl::DLSSMode::eBalanced;
			break;
		case 3:
			dlssMode = sl::DLSSMode::eMaxPerformance;
			break;
		case 4:
			dlssMode = sl::DLSSMode::eUltraPerformance;
			break;
		default:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		}

		sl::DLSSOptions options{};
		options.mode = dlssMode;
		options.outputWidth = a_outputWidth;
		options.outputHeight = a_outputHeight;
		// The Vulkan scene chain is always FP16 HDR.
		options.colorBuffersHDR = sl::Boolean::eTrue;
		options.useAutoExposure = sl::Boolean::eTrue;

		// Use the recommended preset for the detected NVIDIA architecture.
		if (isRTXBelow40Series) {
			options.dlaaPreset = sl::DLSSPreset::ePresetJ;
			options.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			options.qualityPreset = sl::DLSSPreset::ePresetJ;
			options.balancedPreset = sl::DLSSPreset::ePresetJ;
			options.performancePreset = sl::DLSSPreset::ePresetJ;
			options.ultraPerformancePreset = sl::DLSSPreset::ePresetM;
		} else if (isNvidiaGPU) {
			options.dlaaPreset = sl::DLSSPreset::ePresetJ;
			options.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			options.qualityPreset = sl::DLSSPreset::ePresetM;
			options.balancedPreset = sl::DLSSPreset::ePresetM;
			options.performancePreset = sl::DLSSPreset::ePresetM;
			options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
		}

		static bool s_loggedPreset = false;
		if (!s_loggedPreset) {
			s_loggedPreset = true;
			logger::info("[Streamline] DLSS presets set (mode {}): quality={} (RTX40+={} below40={})",
				static_cast<int>(dlssMode), static_cast<int>(options.qualityPreset),
				isNvidiaGPU && !isRTXBelow40Series, isRTXBelow40Series);
		}

		const sl::Result optionsResult = g_sl.slDLSSSetOptions(g_sl.viewport, options);
		if (optionsResult != sl::Result::eOk) {
			logger::error("[Streamline] DLSS options failed (result {})", static_cast<int>(optionsResult));
			return result;
		}

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureDLSS, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			nullptr, &outputReady, &evaluationSkipped);
		result = cs_ClassifyEvaluation(evalRes, outputReady, evaluationSkipped);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] DLSS evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS dispatch faulted — Streamline disabled for this session");
	}
	return result;
}

Streamline::EvaluationResult Streamline::EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	bool outputReady = false;
	bool evaluationSkipped = false;
	EvaluationResult result = EvaluationResult::kFailed;
	if (!initialized || !featureXeSS || g_sl.dispatchFaulted)
		return result;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return result;

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return result;

	__try {
		sl::XeSSMode xessMode = sl::XeSSMode::eQuality;
		switch (a_qualityMode) {
		case 0:
			xessMode = sl::XeSSMode::eNativeAA;
			break;
		case 1:
			xessMode = sl::XeSSMode::eQuality;
			break;
		case 2:
			xessMode = sl::XeSSMode::eBalanced;
			break;
		case 3:
			xessMode = sl::XeSSMode::ePerformance;
			break;
		case 4:
			xessMode = sl::XeSSMode::eUltraPerformance;
			break;
		}

		sl::XeSSOptions xessOpts{};
		xessOpts.mode = xessMode;
		xessOpts.outputWidth = a_outputWidth;
		xessOpts.outputHeight = a_outputHeight;
		xessOpts.sharpness = a_sharpness;
		xessOpts.colorBuffersHDR = sl::Boolean::eTrue;
		const sl::Result optionsResult = g_sl.slXeSSSetOptions(g_sl.viewport, xessOpts);
		if (optionsResult != sl::Result::eOk) {
			logger::error("[Streamline] XeSS options failed (result {})", static_cast<int>(optionsResult));
			return result;
		}

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureXeSS, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			nullptr, &outputReady, &evaluationSkipped);
		result = cs_ClassifyEvaluation(evalRes, outputReady, evaluationSkipped);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] XeSS evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] XeSS dispatch faulted — Streamline disabled for this session");
	}
	return result;
}

Streamline::EvaluationResult Streamline::EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	bool outputReady = false;
	bool evaluationSkipped = false;
	EvaluationResult result = EvaluationResult::kFailed;
	if (!initialized || !featureFSR || g_sl.dispatchFaulted)
		return result;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return result;

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return result;

	__try {
		sl::FSRMode fsrMode = sl::FSRMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			fsrMode = sl::FSRMode::eNativeAA;
			break;
		case 1:
			fsrMode = sl::FSRMode::eMaxQuality;
			break;
		case 2:
			fsrMode = sl::FSRMode::eBalanced;
			break;
		case 3:
			fsrMode = sl::FSRMode::eMaxPerformance;
			break;
		case 4:
			fsrMode = sl::FSRMode::eUltraPerformance;
			break;
		}

		sl::FSROptions fsrOpts{};
		fsrOpts.mode = fsrMode;
		fsrOpts.outputWidth = a_outputWidth;
		fsrOpts.outputHeight = a_outputHeight;
		fsrOpts.sharpness = a_sharpness;
		fsrOpts.colorBuffersHDR = sl::Boolean::eTrue;
		const sl::Result optionsResult = g_sl.slFSRSetOptions(g_sl.viewport, fsrOpts);
		if (optionsResult != sl::Result::eOk) {
			logger::error("[Streamline] FSR options failed (result {})", static_cast<int>(optionsResult));
			return result;
		}

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureFSR, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			nullptr, &outputReady, &evaluationSkipped);
		result = cs_ClassifyEvaluation(evalRes, outputReady, evaluationSkipped);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] FSR evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR dispatch faulted — Streamline disabled for this session");
	}
	return result;
}

bool Streamline::EvaluateFSRFrameGen(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	// Isolate FSR frame-generation preparation from viewport 0 upscaling tags and constants.
	if (!initialized || !featureFSRFG || g_sl.dispatchFaulted)
		return false;
	if (!a_depth || !a_motionVectors || !a_hudlessColor)
		return false;

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return false;

	bool evaluationSubmitted = false;
	bool accepted = false;
	__try {
		const sl::ViewportHandle fgViewport{ 1 };
		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureFSR_G, fgViewport,
			nullptr, nullptr, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			a_hudlessColor, &evaluationSubmitted);
		accepted = evalRes == sl::Result::eOk && evaluationSubmitted;

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		if (evalRes != s_loggedRes) {
			s_loggedRes = evalRes;
			logger::info("[Streamline] FSR FG-prepare result={} render={}x{}", static_cast<int>(evalRes), a_renderWidth, a_renderHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR FG-prepare faulted — Streamline disabled for this session");
	}
	return accepted;
}

bool Streamline::SetDLSSGMode(bool a_enable, uint32_t a_displayWidth, uint32_t a_displayHeight,
	uint32_t a_numFramesToGenerate, bool a_autoMode, bool a_dynamic, float a_dynamicTargetFps)
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return false;

	// Do not call the options entry point while DLSS-G is runtime-unloaded.
	if (!g_dlssgCurrentlyLoaded.load(std::memory_order_acquire))
		return false;

	// Clamp the requested multiplier to the reported hardware limit.
	const uint32_t maxFrames = g_sl.dlssgMaxFramesToGenerate.load(std::memory_order_acquire);
	uint32_t numFrames = a_numFramesToGenerate < 1u ? 1u : a_numFramesToGenerate;
	if (maxFrames > 0u && numFrames > maxFrames)
		numFrames = maxFrames;

	// Reissue options each frame; cached values only suppress duplicate logging.
	const bool changed = !(g_sl.dlssgModeCached && g_sl.dlssgModeOn == a_enable &&
		g_sl.dlssgCachedNumFrames == numFrames && g_sl.dlssgCachedAuto == a_autoMode &&
		g_sl.dlssgCachedDynamic == a_dynamic && g_sl.dlssgCachedDynamicFps == a_dynamicTargetFps &&
		g_sl.dlssgCachedDisplayW == a_displayWidth && g_sl.dlssgCachedDisplayH == a_displayHeight);
	const bool wasModeOn = g_sl.dlssgModeOn;

	bool succeeded = false;
	__try {
		sl::DLSSGOptions options{};
		options.mode = !a_enable ? sl::DLSSGMode::eOff :
		               a_dynamic ? sl::DLSSGMode::eDynamic :
		               a_autoMode ? sl::DLSSGMode::eAuto :
		                            sl::DLSSGMode::eOn;
		options.numFramesToGenerate = numFrames;
		if (a_dynamic)
			options.dynamicTargetFrameRate = a_dynamicTargetFps;
		// Retain resources across temporary loading-screen and menu disables.
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		options.mvecDepthWidth = a_displayWidth;
		options.mvecDepthHeight = a_displayHeight;
		options.colorWidth = a_displayWidth;
		options.colorHeight = a_displayHeight;
		// Volatile inputs are copied into Streamline-owned resources before present.
		//
		// eBlockNoClientQueues is the faster-sounding option and was what this used, but Streamline
		// only permits it if the client waits on DLSSGState::inputsProcessingCompletionFence before
		// modifying or destroying any resource tagged as a DLSS-G input. CS never reads that fence,
		// so it can overwrite depth, motion vectors or the hudless colour while DLSS-G is still
		// consuming them for the previously presented frame. The symptom of that race is an
		// occasional corrupt generated frame rather than a measurable slowdown.
		//
		// The queue parallelism it buys does not show up here in any case, and the two things it
		// might plausibly have bought were checked separately. It does not raise throughput: with
		// eBlockNoClientQueues the rendered rate stayed at exactly 30.0 fps against a 60 Hz mode,
		// identical to the default, because what pins the rendered rate is sl.dlss_g pacing presents
		// to the display refresh -- not the client queue being blocked. (Forcing a tearing present
		// mode and forcing eDynamic with a 240 fps target were also both measured and changed
		// nothing; the latter simply stopped generating.) Nor does it smooth delivery: six paired
		// runs at the refresh rate, a fresh game instance each, were indistinguishable -- frame-time
		// deviation 0.012 / 0.014 / 0.012 ms against 0.013 / 0.014 / 0.019 ms, with GPU-busy differences
		// swamped by which way the camera happened to be facing. The mode was trading a correctness
		// guarantee for nothing measurable, so take the documented default, whose contract CS meets.
		options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
		const sl::Result res = g_sl.slDLSSGSetOptions(g_sl.viewport, options);
		if (res != sl::Result::eOk) {
			if (changed)
				logger::warn("[Streamline] slDLSSGSetOptions failed (result {})", static_cast<int>(res));
		} else {
			succeeded = true;
			g_sl.dlssgModeCached = true;
			g_sl.dlssgModeOn = a_enable;
			g_sl.dlssgCachedNumFrames = numFrames;
			g_sl.dlssgCachedAuto = a_autoMode;
			g_sl.dlssgCachedDynamic = a_dynamic;
			g_sl.dlssgCachedDynamicFps = a_dynamicTargetFps;
			g_sl.dlssgCachedDisplayW = a_displayWidth;
			g_sl.dlssgCachedDisplayH = a_displayHeight;
			if (!a_enable || !wasModeOn)
				g_sl.dlssgCloneTagsPrimed.store(false, std::memory_order_release);
			if (changed)
				logger::info("[Streamline] DLSS-G mode={} ({}) numFrames={} targetFps={} (max {}) display={}x{}", a_enable,
					!a_enable ? "off" : a_dynamic ? "dynamic" : a_autoMode ? "auto" : "on", numFrames, a_dynamicTargetFps, maxFrames,
					a_displayWidth, a_displayHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G SetOptions faulted — Streamline disabled for this session");
	}
	return succeeded;
}

bool Streamline::SetFSRFrameGen(bool a_enable, bool a_hdr,
	bool a_debugView, bool a_debugTearLines, bool a_debugPacingLines, bool a_onlyPresentGenerated)
{
	// The caller retries until the runtime-loaded plugin accepts the option.
	if (!initialized || !featureFSRFG || !g_sl.slFSRFrameGenerationSetOptions || g_sl.dispatchFaulted)
		return false;
	if (!g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire))
		return false;

	bool ok = false;
	__try {
		sl::FSRFrameGenOptions options{};
		options.enabled = a_enable ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.colorBuffersHDR = a_hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugView = a_debugView ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugTearLines = a_debugTearLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugPacingLines = a_debugPacingLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.onlyPresentGenerated = a_onlyPresentGenerated ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		const sl::Result res = g_sl.slFSRFrameGenerationSetOptions(g_sl.viewport, options);
		if (res != sl::Result::eOk) {
			logger::error("[Streamline] slFSRFrameGenerationSetOptions failed (result {})", static_cast<int>(res));
		} else {
			ok = true;
			g_fsrfgOwnsPresent.store(a_enable, std::memory_order_release);
			if (!a_enable)
				g_sl.frameGenerationMultiplier.store(1, std::memory_order_release);
			logger::info("[Streamline] FSR frame generation {}", a_enable ? "enabled" : "disabled");
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR SetFrameGen faulted — Streamline disabled for this session");
	}
	return ok;
}

void Streamline::CaptureFSRFrameGenState()
{
	if (!initialized || !featureFSRFG || !g_sl.slFSRGetFrameGenState || g_sl.dispatchFaulted)
		return;
	__try {
		sl::FSRFrameGenState state{};
		const sl::Result res = g_sl.slFSRGetFrameGenState(g_sl.viewport, state);
		if (res == sl::Result::eOk) {
			g_sl.frameGenerationMultiplier.store(
				std::max(state.numFramesActuallyPresented, 1u), std::memory_order_release);
			g_sl.fsrTotalPresentedFrames.store(state.totalPresentedFrames, std::memory_order_release);
			// FSR-FG had no equivalent of the DLSS-G present-state log, so "frame generation is on
			// but generating nothing" was invisible on this path and the overlay's Post-FG FPS was
			// the only clue. Sample on the same cadence DLSS-G uses.
			static uint32_t s_fsrSampleTick = 0u;
			if ((++s_fsrSampleTick % 600u) == 0u) {
				logger::info("[Streamline] FSR-FG presenting {} frame(s), total presented {}",
					state.numFramesActuallyPresented, state.totalPresentedFrames);
			}
		} else {
			static sl::Result s_lastFsrStateRes = sl::Result::eOk;
			if (res != s_lastFsrStateRes) {
				s_lastFsrStateRes = res;
				logger::warn("[Streamline] slFSRGetFrameGenState failed (result {}) - Post-FG FPS falls back to an estimate",
					static_cast<int>(res));
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

uint64_t Streamline::GetTotalPresentedFrames() const
{
	return g_sl.fsrTotalPresentedFrames.load(std::memory_order_acquire);
}

void Streamline::QueryDLSSGCapabilities()
{
	// Streamline requires this state query on the present thread.
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted ||
		!g_dlssgCurrentlyLoaded.load(std::memory_order_acquire))
		return;
	if (g_sl.dlssgMaxFramesToGenerate.load(std::memory_order_acquire) != 0u)
		return;
	__try {
		sl::DLSSGState state{};
		if (g_sl.slDLSSGGetState(g_sl.viewport, state, nullptr) == sl::Result::eOk && state.numFramesToGenerateMax > 0u) {
			g_sl.dlssgMaxFramesToGenerate.store(state.numFramesToGenerateMax, std::memory_order_release);
			g_sl.dlssgDynamicSupported.store(state.bIsDynamicMFGSupported == sl::Boolean::eTrue, std::memory_order_release);
			logger::info("[Streamline] DLSS-G numFramesToGenerateMax = {} (max {}x multiplier), DynamicMFG supported = {}",
				state.numFramesToGenerateMax, state.numFramesToGenerateMax + 1u, state.bIsDynamicMFGSupported == sl::Boolean::eTrue);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

uint32_t Streamline::GetDLSSGMaxFramesToGenerate() const
{
	return g_sl.dlssgMaxFramesToGenerate.load(std::memory_order_acquire);
}

uint32_t Streamline::GetFrameGenerationMultiplier() const
{
	return g_sl.frameGenerationMultiplier.load(std::memory_order_acquire);
}

bool Streamline::IsDLSSGDynamicSupported() const
{
	return g_sl.dlssgDynamicSupported.load(std::memory_order_acquire);
}

// Names the precondition that pushed DLSS-G onto the passthrough path. Every early return in
// TagDLSSGResources used to be silent, which made "frame generation is on but nothing is
// generated" -- which still costs the latency of the frame-generation present path -- look
// identical to a dozen unrelated causes. Logs only when the reason changes, so it cannot spam.
static void cs_NoteDlssgTagSkip(const char* a_reason)
{
	static const char* s_last = nullptr;
	if (s_last == a_reason)
		return;
	const char* const prev = s_last;
	s_last = a_reason;
	if (a_reason)
		logger::warn("[Streamline] DLSS-G tagging real inputs SKIPPED ({}) - passthrough present, no frames generated", a_reason);
	else if (prev)
		logger::info("[Streamline] DLSS-G tagging real inputs again");
}

void Streamline::TagDLSSGResources(
ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight)
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted) {
		cs_NoteDlssgTagSkip(g_sl.dispatchFaulted ? "Streamline dispatch faulted" : "DLSS-G unavailable");
		return;
	}
	if (!a_depth || !a_motionVectors) {
		cs_NoteDlssgTagSkip("game gave no depth or motion-vector target");
		return;
	}

	auto* dxvk = DXVKInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady()) {
		cs_NoteDlssgTagSkip("interop command resources not ready");
		return;
	}
	if (!g_sl.dlssgCloneTagsPrimed.load(std::memory_order_acquire)) {
		cs_NoteDlssgTagSkip("clone tags not primed yet");
		ClearDLSSGTags();
		return;
	}

	__try {
		sl::FrameToken* token = RenderFrameToken();
		if (!token) {
			cs_NoteDlssgTagSkip("no render frame token");
			return;
		}

		VkDevice vkDevice = dxvk->GetDevice();
		const cs_VulkanProcAttempt createProcAttempt = cs_GetDeviceProcAddrSEH(
			dxvk->GetDeviceProcAddr(), vkDevice, "vkCreateImageView");
		const cs_VulkanProcAttempt destroyProcAttempt = cs_GetDeviceProcAddrSEH(
			dxvk->GetDeviceProcAddr(), vkDevice, "vkDestroyImageView");
		if (createProcAttempt.exceptionCode || destroyProcAttempt.exceptionCode) {
			g_sl.dispatchFaulted = true;
			return;
		}
		auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(createProcAttempt.function);
		auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(destroyProcAttempt.function);
		if (!vkCreateImageView || !vkDestroyImageView)
			return;
		ID3D11Resource* resources[] = { a_depth, a_motionVectors, a_hudlessColor };
		VkImageView views[3]{};
		uint32_t viewCount = 0;
		bool viewCreationTerminalFault = false;
		const auto destroyViews = [&]() {
			return cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views, viewCount,
				resources, static_cast<uint32_t>(std::size(resources)));
		};
		const auto abandonViewsAfterCreationFailure = [&]() {
			if (!viewCreationTerminalFault) {
				destroyViews();
				return;
			}
			// The destroy entry point already faulted on these handles; calling it again would fault
			// again, so abandon the views rather than retrying the destroy.
			std::fill(std::begin(views), std::end(views), VK_NULL_HANDLE);
		};

		const auto makeResource = [&](ID3D11Resource* a_res, sl::Resource& a_out,
			                          sl::SubresourceRange& a_subresource) {
			if (viewCount >= std::size(views))
				return false;
			VkImageView& view = views[viewCount];
			if (!cs_WrapInteropImage(dxvk, vkDevice, vkCreateImageView, a_res, a_out, a_subresource, view, viewCreationTerminalFault))
				return false;
			++viewCount;
			return true;
		};

		sl::Resource depthRes{}, mvecRes{};
		sl::SubresourceRange depthRange{}, mvecRange{}, hudlessRange{};
		if (!makeResource(a_depth, depthRes, depthRange) ||
			!makeResource(a_motionVectors, mvecRes, mvecRange)) {
			cs_NoteDlssgTagSkip("could not wrap depth/motion-vector resources for Vulkan");
			abandonViewsAfterCreationFailure();
			return;
		}

		sl::Extent extent{};
		extent.width = a_renderWidth;
		extent.height = a_renderHeight;

		sl::ResourceTag tags[3];
		uint32_t tagCount = 0;
		tags[tagCount++] = { &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extent };
		tags[tagCount++] = { &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extent };

		// HUD-less color uses display dimensions rather than the render subrect.
		sl::Extent displayExtent{};
		displayExtent.width = a_displayWidth;
		displayExtent.height = a_displayHeight;
		sl::Resource hudlessRes{};
		const uint32_t viewsBeforeHudless = viewCount;
		if (a_hudlessColor) {
			if (makeResource(a_hudlessColor, hudlessRes, hudlessRange)) {
				tags[tagCount++] = { &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &displayExtent };
			} else if (g_sl.dispatchFaulted.load(std::memory_order_acquire) ||
				viewCount != viewsBeforeHudless) {
				abandonViewsAfterCreationFailure();
				return;
			} else {
				tags[tagCount++] = { nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr };
			}
		} else {
			// Clear stale HUD-less input when capture is unavailable.
			tags[tagCount++] = { nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr };
		}

		sl::Result tagResult = sl::Result::eErrorNotInitialized;
		bool lifetimesRetained = false;
		if (cs_SubmitPresentTags(dxvk, *token, g_sl.viewport, tags, tagCount,
				views, viewCount, resources, static_cast<uint32_t>(std::size(resources)), tagResult,
				lifetimesRetained, cs_WantPresentWaitSignal(dxvk))) {
			g_sl.dlssgTaggedThisFrame = true;
			cs_NoteDlssgTagSkip(nullptr);
		} else {
			cs_NoteDlssgTagSkip("tag submission rejected");
			if (!lifetimesRetained)
				destroyViews();
			logger::error("[Streamline] DLSS-G present tagging failed (last tag result {})",
				static_cast<int>(tagResult));
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G tag faulted — Streamline disabled for this session");
	}
}

void Streamline::ClearDLSSGTags()
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;

	__try {
		sl::FrameToken* token = RenderFrameToken();
		if (!token)
			return;

		// Null tags force passthrough when interpolation inputs are unavailable.
		sl::ResourceTag tags[] = {
			sl::ResourceTag{ nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
		};
		auto* dxvk = DXVKInterop::GetSingleton();
		if (!dxvk->CommandResourcesReady())
			return;
		sl::Result tagResult = sl::Result::eErrorNotInitialized;
		bool lifetimesRetained = false;
		if (cs_SubmitPresentTags(dxvk, *token, g_sl.viewport, tags,
				static_cast<uint32_t>(std::size(tags)), nullptr, 0, nullptr, 0, tagResult,
				lifetimesRetained, cs_WantPresentWaitSignal(dxvk))) {
			g_sl.dlssgTaggedThisFrame = true;
		} else {
			logger::error("[Streamline] DLSS-G passthrough tag submission failed (result {})",
				static_cast<int>(tagResult));
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G clear-tags faulted — Streamline disabled for this session");
	}
}

bool Streamline::EnsureDLSSGPresentTag()
{
	// Supply passthrough tags when the render pass did not provide interpolation inputs.
	//
	// featureDLSSG only says DLSS-G is SUPPORTED. Tagging here while it is not the present owner
	// registers a present-wait semaphore that no present will ever consume, so DXVK reports it
	// still pending and the ring is quarantined and rebuilt -- behind a vkDeviceWaitIdle -- once
	// per frame. That is what made the game stutter and flicker after frame generation was turned
	// off, and across a method switch.
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted ||
		!g_dlssgCurrentlyLoaded.load(std::memory_order_acquire))
		return false;
	// Track the passthrough fallback separately from the reasons above: this also catches the
	// cases where TagDLSSGResources was never called at all (paused, a menu is open, or the
	// presenter was not ready), which otherwise leave no trace and read in the log exactly like
	// healthy frame generation while generating nothing.
	const bool hadRealTags = g_sl.dlssgTaggedThisFrame;
	if (!hadRealTags)
		ClearDLSSGTags();
	{
		static bool s_lastHadRealTags = true;
		if (s_lastHadRealTags != hadRealTags) {
			s_lastHadRealTags = hadRealTags;
			if (hadRealTags)
				logger::info("[Streamline] DLSS-G interpolation inputs restored");
			else
				logger::warn("[Streamline] DLSS-G falling back to passthrough tags - render pass submitted no interpolation inputs this frame");
		}
	}
	return g_sl.dlssgTaggedThisFrame;
}

void Streamline::RegisterDxvkOwnershipPredicate()
{
	// DXVK no longer asks who owns the present loop: it always presents as though something may
	// have interposed, so there is no ownership predicate to register any more.
	if (!g_dxvk.module) {
		logger::warn("[Streamline] DXVK module not loaded — cannot register swapchain callbacks");
		return;
	}

	// Streamline features may only be loaded or unloaded while no swapchain exists.
	if (g_dxvk.setSwapchainTornDownCallback) {
		g_dxvk.setSwapchainTornDownCallback(&DxvkSwapchainTornDownCallback);
		logger::info("[Streamline] registered DXVK swapchain-torn-down callback");
	} else {
		logger::warn("[Streamline] dxvkSetSwapchainTornDownCallback not found — frame-generation switching disabled");
	}
}

bool Streamline::HasDispatchFaulted() const
{
	return g_sl.dispatchFaulted.load(std::memory_order_acquire);
}

void Streamline::SetDLSSGDesiredLoaded(bool a_loaded)
{
	g_dlssgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool Streamline::IsDLSSGLoaded() const
{
	return g_dlssgCurrentlyLoaded.load(std::memory_order_acquire);
}

bool Streamline::IsDLSSGLoadSettled() const
{
	return g_dlssgDesiredLoaded.load(std::memory_order_acquire) ==
	       g_dlssgCurrentlyLoaded.load(std::memory_order_acquire);
}

void Streamline::SetFSRFGDesiredLoaded(bool a_loaded)
{
	g_fsrfgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool Streamline::IsFSRFGLoaded() const
{
	return g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire);
}

bool Streamline::IsFSRFGLoadSettled() const
{
	return g_fsrfgDesiredLoaded.load(std::memory_order_acquire) ==
	       g_fsrfgCurrentlyLoaded.load(std::memory_order_acquire);
}

bool Streamline::IsFSRFGPresentOwner() const
{
	return g_fsrfgOwnsPresent.load(std::memory_order_acquire);
}

void Streamline::RequestDxvkSwapchainRecreate(const char* a_reason)
{
	if (g_dxvk.requestSwapchainRecreate) {
		g_dxvk.requestSwapchainRecreate();
		logger::info("[Streamline] requested DXVK swapchain recreate ({})", a_reason);
	} else {
		logger::warn("[Streamline] dxvkRequestSwapchainRecreate not found — {} cannot take effect", a_reason);
	}
}

void Streamline::PushDxvkSyncPresent(bool a_sync)
{
	// Frame-generation proxies require present to complete before the D3D11 hook returns.
	if (g_dxvk.setSyncPresent) {
		g_dxvk.setSyncPresent(a_sync ? 1u : 0u);
	} else {
		static bool s_warned = false;
		if (!s_warned) {
			s_warned = true;
			logger::warn("[Streamline] dxvkSetSyncPresent not found - synchronous present control inactive");
		}
	}
}

void Streamline::PushDxvkPresentQueueDepth(uint32_t a_depth)
{
	// Bounded overlap for a steady-state frame-generation proxy. dxvkSetSyncPresent(true) pins the
	// depth to zero, which makes D3D11SwapChain::PresentImage drain its own present status every
	// frame -- the render thread blocks in waitForSubmission until the proxy's intercepted
	// vkQueuePresentKHR returns. Sampling the render thread with FSR-FG active showed 23% of it
	// parked in SleepConditionVariableSRW for exactly that reason, with the GPU idle 33% of the
	// frame. A small depth lets the next frame be recorded while the proxy finishes presenting the
	// previous one, without unbounding the overlap.
	if (g_dxvk.setPresentQueueDepth) {
		g_dxvk.setPresentQueueDepth(a_depth);
	} else {
		static bool s_warned = false;
		if (!s_warned) {
			s_warned = true;
			logger::warn("[Streamline] dxvkSetPresentQueueDepth not found - bounded present overlap inactive");
		}
	}
}

void Streamline::PushDxvkTearingPreference(uint32_t a_preference)
{
	// 0 = tear-free (MAILBOX), 1 = tearing (IMMEDIATE), anything else = defer to dxvk.conf.
	// Read in Presenter::pickPresentMode, so it must be set before the swapchain recreate that
	// installs a frame-generation proxy.
	//
	// DLSS-G needs tear-free flips: NVIDIA's flip metering cannot space the generated frame when
	// presents tear, and with IMMEDIATE the real and generated frames land back to back -- measured
	// 49.6% of present intervals at 0.0 ms with the rest near 48.5 ms instead of an even 24.2 ms,
	// costing 5.1% of presents to drops. Nothing else wants it: MAILBOX caps at the refresh rate, so
	// a target equal to the refresh has no headroom and loses ~3%.
	if (g_dxvk.setTearingPreference) {
		g_dxvk.setTearingPreference(a_preference);
	} else {
		static bool s_warned = false;
		if (!s_warned) {
			s_warned = true;
			logger::warn("[Streamline] dxvkSetTearingPreference not found - present-mode control inactive");
		}
	}
}
