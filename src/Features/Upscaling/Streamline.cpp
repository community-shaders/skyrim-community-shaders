#include "Streamline.h"

#include "DxvkInterop.h"
#include "FrameGenController.h"

#include "../Upscaling.h"

#include "../../DxvkLoader.h"
#include "../../Globals.h"
#include "../../State.h"
#include "../../Utils/Game.h"

#include <cmath>
#include <cstring>
#include <filesystem>

// Streamline SDK headers (header-only in the repo; the plugin DLLs ship separately
// into the CS folder for NVIDIA users). NV_WINDOWS selects the Win32 surface.
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
#include <sl_helpers_vk.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

namespace
{
	// All Streamline state lives here so Streamline.h need not expose sl.h.
	struct SLState
	{
		HMODULE interposer = nullptr;

		// Core interposer entry points (resolved via GetProcAddress).
		PFun_slInit* slInit = nullptr;
		PFun_slShutdown* slShutdown = nullptr;
		PFun_slIsFeatureSupported* slIsFeatureSupported = nullptr;
		PFun_slGetFeatureRequirements* slGetFeatureRequirements = nullptr;
		PFun_slGetNewFrameToken* slGetNewFrameToken = nullptr;
		PFun_slSetTagForFrame* slSetTagForFrame = nullptr;
		PFun_slSetConstants* slSetConstants = nullptr;
		PFun_slEvaluateFeature* slEvaluateFeature = nullptr;
		PFun_slGetFeatureFunction* slGetFeatureFunction = nullptr;
		PFun_slAllocateResources* slAllocateResources = nullptr;
		PFun_slFreeResources* slFreeResources = nullptr;
		PFun_slSetFeatureLoaded* slSetFeatureLoaded = nullptr;  // runtime DLSS-G (un)load, bracketed by swapchain recreate

		// Feature-specific entry points (resolved via slGetFeatureFunction).
		PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings = nullptr;
		PFun_slDLSSSetOptions* slDLSSSetOptions = nullptr;
		PFun_slReflexSetOptions* slReflexSetOptions = nullptr;
		PFun_slReflexSleep* slReflexSleep = nullptr;
		PFun_slReflexGetState* slReflexGetState = nullptr;
		PFun_slPCLSetMarker* slPCLSetMarker = nullptr;
		PFun_slDLSSGSetOptions* slDLSSGSetOptions = nullptr;
		PFun_slDLSSGGetState* slDLSSGGetState = nullptr;
		PFun_slFSRSetOptions* slFSRSetOptions = nullptr;
		PFun_slFSRFrameGenerationSetOptions* slFSRFrameGenerationSetOptions = nullptr;
		PFun_slFSRGetFrameGenState* slFSRGetFrameGenState = nullptr;
		PFun_slXeSSSetOptions* slXeSSSetOptions = nullptr;

		sl::ViewportHandle viewport{ 0 };

		// EXPLICIT frame identity, matching NVIDIA's Streamline_Sample: every SL call fetches its
		// token fresh via slGetNewFrameToken(token, &id) with the frame ID of the frame that call
		// belongs to. There is NO shared mutable token and NO cross-thread latch — the previous
		// shared-token + render-latch design raced under CPU run-ahead (non-atomic latch released
		// by the present path while the render thread re-latched), straddling constants/tags/present
		// across frame tokens: DLSS-G then correlated the presented frame with another frame's
		// camera constants (whole static world flagged is_dynamic, scaling with camera motion).
		//  * renderFrameId: the render frame currently being built. Written ONCE per frame on the
		//    render thread (BeginRenderFrame <- Main_UpdateJitter hook) from State::frameCount and
		//    read only by render-thread SL calls (constants, tags, evaluates, SimEnd/RenderSubmit/
		//    Present markers). State::Reset() increments frameCount at the TOP of the present hook,
		//    but renderFrameId keeps the presented frame's ID until the next BeginRenderFrame — so
		//    the present markers still carry the frame actually being presented.
		//  * Input-thread calls (SimulationStart, Reflex sleep) use State::frameCountAtomic + 1:
		//    the frame the input thread is simulating (the one the render thread builds next).
		uint32_t renderFrameId = 0;

		// DXVK present-marker bridge (dxvkPushPresentAppFrameId export). When resolved, the
		// PresentStart/End PCL markers are fired by DXVK's submit thread around the REAL
		// vkQueuePresentKHR — with the frame id pushed at the D3D11 Present call — instead of on
		// the render thread at the D3D11 call. The D3D11 call only queues the present; firing the
		// markers there lets the render thread advance to the next frame before the present
		// executes, and SL's present proxy then pairs the presented frame with the NEXT frame's
		// constants (the world-flash under async evaluate). nullptr => legacy render-thread markers.
		void (*dxvkPushPresentAppFrameId)(uint64_t) = nullptr;

		// Latches off after a dispatch fault so a single SEH fault (e.g. a driver
		// mismatch on an untested NVIDIA path) can't crash the game every frame.
		bool dispatchFaulted = false;

		// Cached Reflex options to avoid redundant slReflexSetOptions calls.
		bool reflexCacheValid = false;
		sl::ReflexMode reflexCachedMode = sl::ReflexMode::eOff;
		uint32_t reflexCachedFrameLimitUs = 0;

		// Cached DLSS-G interpolation mode + the render/display dims it was issued for. SetDLSSGMode is called
		// every frame with the gameplay-state gate (on in-world, off while loading/paused/in-menu per the DLSS-G
		// guide §13), so cache and only issue slDLSSGSetOptions on a real change. The DIMS are part of the key:
		// changing upscaler or quality changes the render size (and whether DRS is active), which flips the
		// eDynamicResolutionEnabled flag + dynamicRes — re-issuing only on the on/off edge would leave stale
		// options (a sub-display dynamicRes feeding full-res TAA inputs device-loses; full-res options feeding a
		// DRS upscaler stops doubling).
		bool dlssgModeCached = false;
		bool dlssgModeOn = false;
		uint32_t dlssgCachedDisplayW = 0, dlssgCachedDisplayH = 0;
		// Multi Frame Generation: cached numFramesToGenerate + auto-mode are part of the options key, so a
		// multiplier/mode change re-issues slDLSSGSetOptions. dlssgMaxFramesToGenerate is the hardware cap
		// (numFramesToGenerateMax), queried once on the present thread via QueryDLSSGCapabilities (0 = unknown).
		uint32_t dlssgCachedNumFrames = 0;
		bool dlssgCachedAuto = false;
		bool dlssgCachedDynamic = false;
		float dlssgCachedDynamicFps = 0.0f;
		std::atomic<uint32_t> dlssgMaxFramesToGenerate = 0;
		std::atomic<bool> dlssgDynamicSupported = false;

		// eBlockNoClientQueues input-completion fence (DLSS-G guide §15.1). inputsProcessingCompletionFence is a
		// Vulkan timeline semaphore the plugin signals once it has finished consuming a present's eValidUntilPresent
		// inputs (motion vectors, HUDless). Captured after present (present thread) and host-waited at the next
		// frame start (render thread) before the engine overwrites those live targets. Atomics because the capture
		// (present thread), the wait (render thread), and the reset (DXVK torn-down thread) all touch them; in the
		// single-renderer Skyrim pipeline capture+wait are actually the same thread, so the wait sees the value the
		// preceding present stored. dlssgInputFenceWaited is the last value already waited (skip redundant waits).
		std::atomic<void*> dlssgInputFence{ nullptr };
		std::atomic<uint64_t> dlssgInputFenceValue{ 0 };
		std::atomic<uint64_t> dlssgInputFenceWaited{ 0 };
		PFN_vkWaitSemaphores vkWaitSemaphores = nullptr;

		// Whether a VALID DLSS-G input tag was set this frame (in the render pass). Reset on the render thread
		// at frame start (BeginRenderFrame); set by TagDLSSGResources. If still false at present,
		// the present path sets a null/passthrough tag — SL's present hook requires a tag every present or it stalls.
		bool dlssgTaggedThisFrame = false;

		// Cached VkImageViews for the DLSS-G tagged resources (0=depth, 1=motion, 2=hudless). SL's
		// Vulkan backend requires a view per tagged resource. These must PERSIST after the non-blocking
		// submit because DLSS-G consumes them at present time. 4 entries per slot: depth alternates
		// kMAIN/kMAIN_COPY and motion cycles the caller's 3-deep MV ring, so every steady-state image
		// keeps a live view — a one-entry cache would destroy+recreate a view on EVERY tag while an
		// in-flight generation may still read the old one. Eviction (round-robin) only happens when
		// engine targets are recreated (resolution change, behind an interop-ring drain); freed at Shutdown.
		struct {
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
		} dlssgViewCache[3][4];
		uint32_t dlssgViewEvict[3] = {};
	} g_sl;

	// DLSS-G runtime load state (Streamline DLSS-G guide §18). On DLSS-G hardware the plugin is
	// loaded at slInit (current=true, set in Initialize); on all other hardware it is never in
	// featuresToLoad at all, so both start false and every transition below is a no-op — the
	// FrameGenController must never see a phantom "loaded" state it then can't unload (the
	// unload would never settle and block FSR-FG delivery behind the phase).
	// CS sets `desired` on a real select/deselect and requests a DXVK swapchain recreate; DXVK's torn-down
	// callback below applies slSetFeatureLoaded while no swapchain exists, so the next vkCreateSwapchainKHR
	// installs/omits DLSS-G's present proxy + extra queue. Unloading when off removes the queue + off-screen
	// copy overhead the guide warns about.
	std::atomic<bool> g_dlssgDesiredLoaded{ false };
	bool g_dlssgCurrentlyLoaded = false;
	// FSR3 frame generation (sl.fsr_g / kFeatureFSR_G) is now a load-toggled feature EXACTLY like DLSS-G:
	// loading it activates its WSI hooks (which install the FFX FrameInterpolationSwapChain), unloading
	// removes them. Only ONE FG feature is ever desired-loaded at a time (the FrameGen controller enforces
	// it), so a method switch unloads the outgoing feature and loads the incoming one in the SAME
	// swapchain-recreate window. Both start loaded at slInit (both in featuresToLoad); the controller
	// unloads the non-selected one on its first reconcile.
	std::atomic<bool> g_fsrfgDesiredLoaded{ false };
	bool g_fsrfgCurrentlyLoaded = false;

	// Apply one FG feature's desired loaded-state via slSetFeatureLoaded, re-resolving its entry points on
	// load. Free function (not a lambda) so the SEH __try has no C++ object unwinding in scope. Called from
	// DxvkSwapchainTornDownCallback for BOTH FG features in the no-swapchain window the guide requires.
	void ReconcileFgFeatureLoad(sl::Feature a_feature, std::atomic<bool>& a_desired, bool& a_current)
	{
		const bool want = a_desired.load(std::memory_order_acquire);
		if (want == a_current || !g_sl.slSetFeatureLoaded || g_sl.dispatchFaulted)
			return;
		__try {
			if (g_sl.slSetFeatureLoaded(a_feature, want) != sl::Result::eOk)
				return;
			a_current = want;
			// A reloaded plugin may sit at a new base — re-resolve its entry points (plugin option state is
			// gone after an unload).
			if (want) {
				if (a_feature == sl::kFeatureDLSS_G) {
					g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSGSetOptions));
					g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(g_sl.slDLSSGGetState));
				} else if (a_feature == sl::kFeatureFSR_G) {
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationSetOptions));
					g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRGetFrameGenState", reinterpret_cast<void*&>(g_sl.slFSRGetFrameGenState));
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_sl.dispatchFaulted = true;
		}
	}

	// Invoked by DXVK inside recreateSwapChain() between destroy and create (registered via
	// dxvkSetSwapchainTornDownCallback). Toggles each FG feature's loaded state to match `desired` in
	// exactly the window the guide requires. Runs on DXVK's present/acquire thread under its surface lock.
	void DxvkSwapchainTornDownCallback()
	{
		// ANY swapchain teardown (load/unload OR a plain resize/fullscreen recreate) invalidates DLSS-G's
		// per-swapchain option state, so force the next SetDLSSGMode to re-issue slDLSSGSetOptions against the
		// new swapchain dimensions. Without this, a plain recreate (desired==current) would leave the mode
		// cached and the first post-resize SetDLSSGMode suppressed.
		g_sl.dlssgModeCached = false;
		g_sl.dlssgModeOn = false;

		// The DLSS-G plugin's input-completion timeline semaphore is per-swapchain: any teardown invalidates it,
		// so drop the captured handle/value and the waited watermark. The next capture re-reads a fresh semaphore
		// and WaitDLSSGInputFence starts clean (a stale watermark could otherwise skip a needed wait on reload).
		g_sl.dlssgInputFence.store(nullptr, std::memory_order_release);
		g_sl.dlssgInputFenceValue.store(0, std::memory_order_release);
		g_sl.dlssgInputFenceWaited.store(0, std::memory_order_release);

		// Reconcile BOTH FG features. On a method switch the controller has set one desired=true and the
		// other desired=false, so this unloads the outgoing feature and loads the incoming one here.
		ReconcileFgFeatureLoad(sl::kFeatureDLSS_G, g_dlssgDesiredLoaded, g_dlssgCurrentlyLoaded);
		ReconcileFgFeatureLoad(sl::kFeatureFSR_G, g_fsrfgDesiredLoaded, g_fsrfgCurrentlyLoaded);
	}

	// Streamline emits a handful of WARN-level diagnostics that are benign for Community Shaders' DXVK
	// full-interposition setup but cannot be fixed without editing NVIDIA's SIGNED SL binaries (verified present
	// and identical from SL 2.10.3 through 2.12.0 — no SDK release removes them). We've individually confirmed
	// each is harmless, so we drop them here rather than letting them spam the log. This filters ONLY these exact,
	// known strings — any other SL warning/error still flows through. Re-audit this list on every SL SDK bump.
	bool IsBenignSLWarning(const char* a_msg)
	{
		if (!a_msg)
			return false;
		static constexpr const char* kBenign[] = {
			// sl.chi/vulkan.cpp: logs "not implemented" but immediately delegates to the working NvLL
			// implementation (NvLL_VK_SetLatencyMarker) — a stale message, not a missing feature.
			"setAsyncFrameMarker is not implemented",
			// sl.common requests Vulkan command-buffer hooks (BeginCommandBuffer/CmdBindPipeline/
			// CmdBindDescriptorSets) the interposer doesn't register; its resource state-tracking is unused on
			// our path and DLSS-G/Reflex function regardless ("will not function properly" is a false alarm here).
			"is NOT supported, plugin will not function properly",
			// RSync is a D3D-only low-latency feature; under Vulkan it never initializes. SL itself tags this
			// "This is probably not a big deal".
			"RSync will not run because it was not initialized",
			// sl.dlss_g first-present bootstrap: the backbuffer extent is briefly 0x0 before the swapchain is
			// known; SL resets it to the full backbuffer size itself. One-shot, self-correcting.
			"Invalid backbuffer resource extent",
			// CS maps the interposer (as DXVK's vulkan-1.dll) before DXVK creates its device, so DXVK touches
			// Vulkan before slInit by construction. slInit is already hoisted as early as an SKSE plugin can.
			"some DX/VK APIs were invoked before slInit",
			// dlss_g present pacing prints this when a frame is unusually long (load screen, alt-tab, our
			// camera-warp test). Cosmetic frame-timer reset.
			"reseting frame timer",
		};
		for (const char* needle : kBenign) {
			if (std::strstr(a_msg, needle))
				return true;
		}
		return false;
	}

	// Routes Streamline's own logging into the CS logger.
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
			return;  // documented-benign NVIDIA SL diagnostic — see IsBenignSLWarning
		switch (a_type) {
		case sl::LogType::eError:
			logger::warn("[Streamline/SL] {}", a_msg);
			break;
		case sl::LogType::eWarn:
			logger::debug("[Streamline/SL] {}", a_msg);
			break;
		default:
			logger::trace("[Streamline/SL] {}", a_msg);
			break;
		}
	}

	// .../SKSE/Plugins/CommunityShaders/streamline — sibling of the DXVK dir,
	// resolved module-relative (MO2 VFS safe) just like DxvkLoader::GetDxvkDir().
	std::filesystem::path GetStreamlineDir()
	{
		const auto dxvkDir = DxvkLoader::GetDxvkDir();
		if (dxvkDir.empty())
			return {};
		return dxvkDir.parent_path() / L"streamline";
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
	// Map sl.interposer.dll EARLY — before DXVK creates its VkInstance — so DXVK's
	// loadVulkanLibrary("sl.interposer.dll") (which we added to its loader) aliases this already-mapped
	// module and routes DXVK's ENTIRE Vulkan surface through Streamline (full interposition). A bare
	// runtime LoadLibraryA from inside dxvk_d3d11.dll does NOT search the CS dxvk/ subfolder, so without
	// this preload DXVK falls through to the real vulkan-1.dll and SL never sees the device/present.
	// LOAD_WITH_ALTERED_SEARCH_PATH lets the interposer resolve its sibling sl.*.dll from the CS folder.
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
	// slInit MUST run before DXVK creates its VkInstance (the interposer's vkCreateInstance/Device proxies
	// add DLSS-G's device requirements + capture the handles). The later SetupResources Initialize() call
	// no-ops (triedInit guard).
	Initialize();
}

// Pre-slInit hardware capability probe: does this system have DLSS-G-class hardware?
// DLSS-G requires the optical-flow accelerator (NVIDIA Ada and newer), exposed as the
// VK_NV_optical_flow device extension. Checked on a throwaway instance created against the
// SYSTEM Vulkan loader (System32), untouched by the interposer — this must be known BEFORE
// slInit: on non-DLSS-G hardware the sl.dlss_g plugin is omitted from featuresToLoad entirely,
// because its swapchain hook rebuilds the very first swapchain create and strips the
// FSE-DISALLOWED pNext that keeps the window on the copy present path FSR-FG requires (the
// driver flip-locks a window at its first flip present, so the boot create decides).
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

	// No GPU-vendor gate: Streamline performs its own per-feature compatibility check
	// (slIsFeatureSupported for DLSS / Reflex / DLSS-G). On hardware or a driver that
	// lacks a feature it simply reports unsupported and that path stays on FSR3-on-DXVK.
	// If the plugin DLLs are absent (the usual case — they are NVIDIA redistributables
	// fetched at build time) the interposer load below fails and we degrade to FSR.
	const auto slDir = GetStreamlineDir();
	if (slDir.empty()) {
		logger::warn("[Streamline] could not resolve plugin directory");
		return false;
	}

	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	// LOAD_WITH_ALTERED_SEARCH_PATH so the interposer resolves its sibling sl.*.dll
	// plugins from the same CS folder rather than the game root / System32.
	// May already be mapped by PreloadInterposer() (so DXVK could alias it as its vulkan-1.dll).
	if (!g_sl.interposer)
		g_sl.interposer = LoadLibraryExW(interposerPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!g_sl.interposer) {
		// Expected on a normal install: the SL plugin DLLs are not bundled (NVIDIA
		// redistributables). Degrade cleanly to FSR.
		logger::info("[Streamline] sl.interposer.dll not present in '{}' — DLSS/Reflex disabled", slDir.string());
		return false;
	}

	const bool resolved =
		Resolve(g_sl.slInit, "slInit") &&
		Resolve(g_sl.slShutdown, "slShutdown") &&
		Resolve(g_sl.slIsFeatureSupported, "slIsFeatureSupported") &&
		Resolve(g_sl.slGetFeatureRequirements, "slGetFeatureRequirements") &&
		Resolve(g_sl.slGetNewFrameToken, "slGetNewFrameToken") &&
		Resolve(g_sl.slSetTagForFrame, "slSetTagForFrame") &&
		Resolve(g_sl.slSetConstants, "slSetConstants") &&
		Resolve(g_sl.slEvaluateFeature, "slEvaluateFeature") &&
		Resolve(g_sl.slGetFeatureFunction, "slGetFeatureFunction") &&
		Resolve(g_sl.slAllocateResources, "slAllocateResources") &&
		Resolve(g_sl.slFreeResources, "slFreeResources");

	// Non-fatal: runtime DLSS-G (un)load (Streamline DLSS-G guide §18 — avoids the extra present queue +
	// off-screen-copy overhead when DLSS-G is off). Absent on older SL builds -> the unload is skipped.
	Resolve(g_sl.slSetFeatureLoaded, "slSetFeatureLoaded");
	if (!resolved) {
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
		return false;
	}

	const auto slDirWide = slDir.wstring();
	const wchar_t* pluginPaths[] = { slDirWide.c_str() };
	// Load all features the host drives. The user switches FG method in-game with NO restart, and the two FG
	// plugins don't fight over the swapchain because CS keeps only ONE of them owning present at a time: sl.dlss_g
	// is loaded/unloaded by FG method (slSetFeatureLoaded in the DXVK swapchain-recreate window — see
	// Upscaling's reconcile), so when FSR-FG owns present sl.dlss_g has no WSI hooks at all. This is what lets the
	// STOCK, UNMODIFIED SL interposer work — no interposer-side hook suppression is needed. They also don't fight
	// over the shared per-frame constants/tags: the FSR FG-prepare runs on a DEDICATED viewport (see
	// EvaluateFSRFrameGen) so its depth/MV tags + camera constants never collide with the upscaler's (which
	// dlss_g also reads on viewport 0).
	// ONE frame-generation method per system, decided by hardware here and never changed:
	// DLSS-G-class hardware loads sl.dlss_g (its swapchain hook makes the window flip-model
	// from the first create — exactly what its pacer needs); all other hardware omits the
	// plugin entirely, so DXVK's FSE-DISALLOWED reaches the driver at the first create and the
	// window stays on the copy present path FSR-FG requires. Both facts are per-window and
	// locked at boot by the driver, which is why the method cannot be a runtime setting.
	dlssgHardware = ProbeDLSSGHardware();

	std::vector<sl::Feature> featuresToLoad = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL,
		sl::kFeatureFSR, sl::kFeatureFSR_G, sl::kFeatureXeSS };
	if (dlssgHardware) {
		featuresToLoad.push_back(sl::kFeatureDLSS_G);
		// slInit loads the plugin, so the §18 load-state tracking starts "loaded" here (and
		// only here — on other hardware the plugin does not exist in this process).
		g_dlssgDesiredLoaded.store(true, std::memory_order_release);
		g_dlssgCurrentlyLoaded = true;
	}
	// kFeatureFSR_G (sl.fsr_g frame gen) is ALWAYS in featuresToLoad, so slInit loads it on every GPU —
	// start its load-state "loaded" too. The FrameGen controller unloads whichever FG feature is not the
	// selected method on its first reconcile, converging to exactly one loaded.
	g_fsrfgDesiredLoaded.store(true, std::memory_order_release);
	g_fsrfgCurrentlyLoaded = true;

	sl::Preferences pref{};
	pref.renderAPI = sl::RenderAPI::eVulkan;
	// Frame-based tagging is required by slEvaluateFeature / slSetTagForFrame.
	pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	// Full interposition: sl.interposer.dll IS DXVK's vulkan-1.dll, so SL's own
	// vkCreateInstance/Device/present proxies run. eUseManualHooking must be OFF.
	pref.featuresToLoad = featuresToLoad.data();
	pref.numFeaturesToLoad = static_cast<uint32_t>(featuresToLoad.size());
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0";
	// projectId is expected to be a GUID string for the eCustom engine path.
	pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
	if (char v[2] = {}; GetEnvironmentVariableA("CS_SL_VERBOSE", v, sizeof(v)) && v[0] == 0x31)
		pref.logLevel = sl::LogLevel::eVerbose;
	else
	pref.logLevel = sl::LogLevel::eDefault;
	// Route SL's own logging through LogCallback into the CS logger (and drop documented-benign SL warnings
	// there). We deliberately do NOT set pref.pathToLogsAndData: that makes the signed SL binaries write their
	// own raw sl.log file, which always contains NVIDIA's internal SL_LOG_WARN diagnostics (setAsyncFrameMarker,
	// command-buffer hook-not-supported, RSync, before-slInit, …) that cannot be removed without editing the
	// signed DLLs and are present in every SL SDK through 2.12.0. The CS-surfaced log (CommunityShaders.log) is
	// the one that matters and stays clean via the callback filter. Re-add pathToLogsAndData only for local SL
	// debugging, with the understanding that the raw file carries NVIDIA's diagnostics verbatim.
	pref.logMessageCallback = &LogCallback;

	const sl::Result res = g_sl.slInit(pref, sl::kSDKVersion);
	if (res != sl::Result::eOk) {
		logger::warn("[Streamline] slInit failed (result {}) — DLSS/Reflex disabled", static_cast<int>(res));
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
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

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk || !dxvk->IsAvailable()) {
		logger::warn("[Streamline] DXVK interop unavailable — cannot hand Vulkan device to SL");
		return;
	}

	// SL already owns instance/device/queues (it created them via its vkCreateInstance/Device proxies
	// as DXVK's loader under full interposition), so no slSetVulkanInfo handoff is needed.
	vulkanDeviceSet = true;

	// Per-adapter feature probe. AdapterInfo with vkPhysicalDevice set keys SL off
	// the actual GPU (LUID ignored). Only eOk means "enable". On AMD this whole
	// method is unreachable (Initialize() returned false), but the per-feature
	// check is the authoritative second gate on NVIDIA (old GPUs lack DLSS-G, etc.).
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
	featureXeSS = supported(sl::kFeatureXeSS);  // Community Shaders sl.xess plugin (any GPU)
	featureFSR = supported(sl::kFeatureFSR);    // Community Shaders sl.fsr plugin — UPSCALE (any GPU)
	featureFSRFG = supported(sl::kFeatureFSR_G);  // Community Shaders sl.fsr_g plugin — FRAME GEN (any GPU)

	// Bind the feature-specific functions (valid only after the device is set).
	if (featureDLSS) {
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", reinterpret_cast<void*&>(g_sl.slDLSSGetOptimalSettings));
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSSetOptions));
		featureDLSS = g_sl.slDLSSSetOptions != nullptr;
	}
	if (featureReflex) {
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", reinterpret_cast<void*&>(g_sl.slReflexSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", reinterpret_cast<void*&>(g_sl.slReflexSleep));
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexGetState", reinterpret_cast<void*&>(g_sl.slReflexGetState));
		featureReflex = g_sl.slReflexSetOptions != nullptr && g_sl.slReflexSleep != nullptr;
	}
	// PCL latency markers (separate feature, loaded by default in slInit). Used to bracket the
	// frame pipeline so Reflex can place its sleep optimally and report PCL stats. GPU-agnostic.
	g_sl.slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", reinterpret_cast<void*&>(g_sl.slPCLSetMarker));
	logger::info("[Streamline] PCL latency markers {}", g_sl.slPCLSetMarker ? "available" : "unavailable");
	if (featureDLSSG) {
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSGSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(g_sl.slDLSSGGetState));
		featureDLSSG = g_sl.slDLSSGSetOptions != nullptr && g_sl.slDLSSGGetState != nullptr;
	}
	if (featureFSR) {
		// sl.fsr is now UPSCALE ONLY (kFeatureFSR).
		g_sl.slGetFeatureFunction(sl::kFeatureFSR, "slFSRSetOptions", reinterpret_cast<void*&>(g_sl.slFSRSetOptions));
		featureFSR = g_sl.slFSRSetOptions != nullptr;
	}
	if (featureFSRFG) {
		// FSR3 frame generation is its own feature/plugin (kFeatureFSR_G / sl.fsr_g), a twin of sl.dlss_g,
		// so its entry points resolve from that feature — NOT kFeatureFSR. Only one FG feature is loaded
		// at a time (see the FrameGen controller), exactly like DLSS-G.
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRGetFrameGenState", reinterpret_cast<void*&>(g_sl.slFSRGetFrameGenState));
		featureFSRFG = g_sl.slFSRFrameGenerationSetOptions != nullptr;
	}
	if (featureXeSS) {
		g_sl.slGetFeatureFunction(sl::kFeatureXeSS, "slXeSSSetOptions", reinterpret_cast<void*&>(g_sl.slXeSSSetOptions));
		featureXeSS = g_sl.slXeSSSetOptions != nullptr;
	}

	// Hardware gate (see Initialize): on non-DLSS-G hardware the plugin was never loaded, so
	// the per-adapter probe above already reported unsupported; this just keeps the invariant
	// explicit (CS_FORCE_FSR_FG also lands here via the pre-slInit hardware probe).
	featureDLSSG = featureDLSSG && dlssgHardware;

	logger::info("[Streamline] feature support: DLSS={} Reflex={} DLSS-G={} FSR={} FSR-G={} XeSS={} (FSR-FG fns {})",
		featureDLSS, featureReflex, featureDLSSG, featureFSR, featureFSRFG, featureXeSS,
		g_sl.slFSRFrameGenerationSetOptions ? "ok" : "missing");

	// Present path: hardware flips for everyone (the dxvk default — FSE pNext not chained).
	// DLSS-G's pacer requires flips, and FSR-FG runs correctly on them (validated in live
	// play; note for the record that idle unattended FSR sessions once showed late wedges on
	// flips — kept under observation, see dxvkSetFsePNextChain for the copy-path escape hatch).

	// Identify the GPU generation from the Vulkan physical device for DLSS render-preset selection.
	// (vkGetPhysicalDeviceProperties carries the real PCI vendor/device IDs even under DXVK, unlike
	// the DXGI adapter desc which the create hook may not capture.)
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

void Streamline::Shutdown()
{
	// Free the cached DLSS-G resource views before tearing down the device.
	if (auto* dxvk = DxvkInterop::GetSingleton(); dxvk && dxvk->IsAvailable()) {
		VkDevice vkDevice = dxvk->GetDevice();
		if (auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
				dxvk->GetDeviceProcAddr()(vkDevice, "vkDestroyImageView"))) {
			for (auto& slot : g_sl.dlssgViewCache) {
				for (auto& c : slot) {
					if (c.view != VK_NULL_HANDLE)
						vkDestroyImageView(vkDevice, c.view, nullptr);
					c.view = VK_NULL_HANDLE;
					c.image = VK_NULL_HANDLE;
				}
			}
		}
	}

	if (g_sl.interposer) {
		if (initialized && g_sl.slShutdown)
			g_sl.slShutdown();
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
	}
	initialized = false;
	vulkanDeviceSet = false;
	featureDLSS = featureReflex = featureDLSSG = featureXeSS = featureFSR = featureFSRFG = false;
}

// Fetch the SL frame token for an EXPLICIT frame ID — the Streamline_Sample pattern (its every
// Reflex/PCL callback and evaluate does `slGetNewFrameToken(temp, &frameID)` with the engine's
// frame counter). SL keeps an internal ring of tokens; fetching the same ID repeatedly returns
// the same token, so per-call fetching is cheap and there is no shared token state to race.
static sl::FrameToken* TokenForFrame(uint32_t a_frameId)
{
	sl::FrameToken* token = nullptr;
	if (g_sl.slGetNewFrameToken(token, &a_frameId) != sl::Result::eOk)
		return nullptr;
	return token;
}

// Token for the render frame currently being built (render-thread SL calls: constants, tags,
// evaluates, SimEnd/RenderSubmit/Present markers). See g_sl.renderFrameId.
static sl::FrameToken* RenderFrameToken()
{
	return TokenForFrame(g_sl.renderFrameId);
}

// Token for the frame the input thread is simulating (SimulationStart marker, Reflex sleep):
// one ahead of the frame the render thread is building.
static uint32_t SimFrameId()
{
	return globals::state->frameCountAtomic.load(std::memory_order_relaxed) + 1;
}

// DXVK submit-thread callback (registered via dxvkSetPresentMarkerCallback): fires the
// PresentStart/PresentEnd PCL markers around the REAL vkQueuePresentKHR with the frame id of the
// frame actually being presented. This is what makes SL's present-to-constants correlation exact
// under async evaluate — the sample gets the same property for free because its Present call IS
// the real present (no presenter-thread indirection).
static void CS_DxvkPresentMarkerBridge(uint64_t a_appFrameId, uint32_t a_phase)
{
	if (!g_sl.slPCLSetMarker || g_sl.dispatchFaulted)
		return;
	__try {
		if (sl::FrameToken* token = TokenForFrame(static_cast<uint32_t>(a_appFrameId)))
			g_sl.slPCLSetMarker(a_phase == 0u ? sl::PCLMarker::ePresentStart : sl::PCLMarker::ePresentEnd, *token);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::BeginRenderFrame()
{
	// Render thread, frame start (Main_UpdateJitter hook). Establishes the explicit frame ID every
	// render-thread SL call this frame will fetch its token with — the sample's engine-frame-counter
	// equivalent. Also the frame-scoped flag reset: doing it here (instead of at PresentEnd) keeps
	// every reader strictly after every writer on this one thread.
	g_sl.renderFrameId = globals::state->frameCount;
	g_sl.dlssgTaggedThisFrame = false;

	// Before this frame renders new motion vectors / HUDless over the live engine targets, wait for the
	// DLSS-G plugin to finish consuming the previous present's inputs (eBlockNoClientQueues contract).
	WaitDLSSGInputFence();
}

void Streamline::CaptureDLSSGInputFence()
{
	// Present thread, after the present. Read the plugin-internal input-completion timeline semaphore + the
	// value for the inputs consumed by the just-presented frame (DLSS-G guide §15.1). Only meaningful while
	// DLSS-G is actually interpolating; slDLSSGGetState must run on the present thread (it does here).
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted || !g_dlssgCurrentlyLoaded || !g_sl.dlssgModeOn)
		return;
	__try {
		sl::DLSSGState state{};
		if (g_sl.slDLSSGGetState(g_sl.viewport, state, nullptr) == sl::Result::eOk) {
			// A different semaphore handle means the plugin re-created it (reload/resize) — reset the waited
			// watermark so the fresh, possibly-lower value is not mistaken for already-waited.
			if (g_sl.dlssgInputFence.exchange(state.inputsProcessingCompletionFence, std::memory_order_acq_rel) !=
				state.inputsProcessingCompletionFence)
				g_sl.dlssgInputFenceWaited.store(0, std::memory_order_release);
			g_sl.dlssgInputFenceValue.store(state.lastPresentInputsProcessingCompletionFenceValue, std::memory_order_release);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::WaitDLSSGInputFence()
{
	// Render thread, frame start. Host-wait the input-completion timeline semaphore for the value captured
	// after the last present, so the plugin's non-presenting-queue read of the eValidUntilPresent inputs
	// (motion vectors, HUDless) has completed before this frame overwrites those live targets. No-op unless
	// DLSS-G is interpolating and a newer value than we already waited on was captured.
	if (!initialized || g_sl.dispatchFaulted || !g_sl.dlssgModeOn)
		return;
	void* fence = g_sl.dlssgInputFence.load(std::memory_order_acquire);
	const uint64_t value = g_sl.dlssgInputFenceValue.load(std::memory_order_acquire);
	if (!fence || value == 0 || value <= g_sl.dlssgInputFenceWaited.load(std::memory_order_acquire))
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	VkDevice device = dxvk->GetDevice();
	if (device == VK_NULL_HANDLE)
		return;
	if (!g_sl.vkWaitSemaphores) {
		g_sl.vkWaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(
			dxvk->GetDeviceProcAddr()(device, "vkWaitSemaphores"));
		if (!g_sl.vkWaitSemaphores)
			return;  // no host timeline-semaphore wait (should never happen on a DXVK 1.3 device) — skip
	}

	__try {
		VkSemaphore sem = reinterpret_cast<VkSemaphore>(fence);
		uint64_t waitValue = value;
		VkSemaphoreWaitInfo wi{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		wi.semaphoreCount = 1;
		wi.pSemaphores = &sem;
		wi.pValues = &waitValue;
		// Bounded 8 ms timeout: this waits on the PREVIOUS present's consumption, which is almost always
		// already done, so the steady-state cost is ~0. A timeout must never wedge the render thread — if it
		// ever fires we proceed (the fence being late implies DLSS-G is stalled elsewhere) and log once.
		const VkResult wr = g_sl.vkWaitSemaphores(device, &wi, 8ull * 1000ull * 1000ull);
		if (wr == VK_SUCCESS)
			g_sl.dlssgInputFenceWaited.store(value, std::memory_order_release);
		else if (wr == VK_TIMEOUT) {
			static bool s_warned = false;
			if (!s_warned) {
				s_warned = true;
				logger::warn("[Streamline] DLSS-G input fence wait timed out (value {}) — proceeding", value);
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::UpdateReflex(bool a_enable, bool a_boost, uint32_t a_frameLimitUs)
{
	// Tractable on the existing DXVK device (markers + sleep only, no extra queues).
	if (!initialized || !featureReflex || g_sl.dispatchFaulted)
		return;

	const sl::ReflexMode mode = !a_enable ? sl::ReflexMode::eOff :
	                            a_boost   ? sl::ReflexMode::eLowLatencyWithBoost :
	                                        sl::ReflexMode::eLowLatency;

	__try {
		if (!g_sl.reflexCacheValid || g_sl.reflexCachedMode != mode || g_sl.reflexCachedFrameLimitUs != a_frameLimitUs) {
			sl::ReflexOptions options{};
			options.mode = mode;
			options.frameLimitUs = a_frameLimitUs;  // Reflex frame limiter (0 = unlimited)
			if (g_sl.slReflexSetOptions(options) == sl::Result::eOk) {
				g_sl.reflexCachedMode = mode;
				g_sl.reflexCachedFrameLimitUs = a_frameLimitUs;
				g_sl.reflexCacheValid = true;
			}
		}
		if (mode != sl::ReflexMode::eOff) {
			// Reflex sleep belongs to the frame the input thread is about to simulate (sample:
			// ReflexCallback_Sleep fetches the token with the app's frameID).
			// ONCE PER RENDERED FRAME: PollInputDevices (our caller) runs more than once per frame, and
			// sleeping on every poll destroys Reflex's pacing (visible judder). Same guard dev carried
			// (lastReflexSleepFrame) — it was lost in the Vulkan-interposition rewrite.
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
		logger::error("[Streamline] Reflex dispatch faulted — disabling for this session");
	}
}

void Streamline::SetPCLMarker(PclMarker a_marker)
{
	if (!initialized || !g_sl.slPCLSetMarker || g_sl.dispatchFaulted)
		return;

	// SimulationStart fires once per INPUT POLL (BSInputDeviceManager::PollInputDevices), and
	// Skyrim polls more than once per rendered frame. Fire the marker only on the FIRST poll of
	// each sim frame — poll-rate SimulationStart markers corrupt the PCL timing Reflex/DLSS-G
	// pace by. Same once-per-frame rule dev enforced around its poll-driven Reflex work.
	uint32_t simFrame = 0;
	if (a_marker == PclMarker::SimulationStart) {
		static uint32_t s_lastSimFrame = UINT32_MAX;
		simFrame = SimFrameId();
		if (s_lastSimFrame == simFrame)
			return;  // repeat poll within the same sim frame: no marker
		s_lastSimFrame = simFrame;
	}

	__try {
		// Render-thread markers carry the render frame's explicit ID so DLSS-G's present —
		// correlated to kMarkerPresentFrame, which the PresentStart marker sets — matches the frame
		// the render thread actually tagged (constants/tags/evaluate all use the same ID).
		// SimulationEnd fires on the RENDER thread in CS (Main_PostProcessing) so it belongs to the
		// render frame too; only SimulationStart (input thread) uses the sim frame's ID.
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
		logger::error("[Streamline] PCL marker faulted — disabling for this session");
	}
}

// ---- Shared upscale / frame-generation evaluate plumbing ---------------------------------------
// EvaluateDLSS / EvaluateXeSS / EvaluateFSR / EvaluateFSRFrameGen differ only in the feature-specific
// options they set up front; the per-frame SL constants, the DXVK->SL interop resource wrapping, the
// resource tagging, and the command-buffer evaluate+submit are identical. Those shared steps live in the
// helpers below so each public Evaluate* is just "set options -> call the core". They are free
// (internal-linkage) functions so this stays out of Streamline.h (which deliberately keeps SL types
// opaque). They are called from inside each Evaluate*'s __try; having no __try of their own, the C++
// objects they use are fine — only the SEH-wrapped callers must keep POD locals.

// Fill SL common constants for the frame: real camera matrices from the frame-buffer cache fed through
// recalculateCameraMatrices, NEGATED jitter (SL/DLSS/FFX sign convention), non-inverted depth, and unit
// mvec scale (Skyrim MVs are already in SL space). Identical inputs for every upscaler and the FG-prepare.
// Returns false when the engine camera data is not yet valid this frame (zero/singular view-proj during
// boot/menu/load frames): Matrix::Invert then yields NaN/INF, and sl::recalculateCameraMatrices' unguarded
// vectorNormalize turns zero camera rows into NaN camera vectors — SL consumes all of it verbatim (the
// NaN clipToPrevClip/prevClipToClip visible in the NGX dev overlay). The caller skips slSetConstants for
// such frames; SL then keeps the last good constants, which is correct for a static/UI frame.
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

	sl::recalculateCameraMatrices(a_consts);  // keep: fills clipToCameraView from cameraViewToClip

	// Override clipToPrevClip/prevClipToClip with a deterministic jitter-free reprojection from the game's OWN
	// current + previous unjittered view-proj, so SL's depth->MV reconstruction reproduces MotionBlur::GetSSMotionVector.
	// recalculateCameraMatrices derives these from a STATIC previous-frame cache (its own header: "DO NOT USE THIS IN
	// ANYTHING PROPER") that aliases across call sites — the upscale evaluate and the FG-prepare both build constants
	// every frame, each clobbering the other's "previous".
	//
	// CAMERA-RELATIVE BRIDGE (critical): Skyrim renders camera-relative. curVP.Invert() reconstructs world relative to
	// the CURRENT camera origin (CameraPosAdjust), but CameraPreviousViewProjUnjittered expects world relative to the
	// PREVIOUS camera origin (CameraPreviousPosAdjust) — the engine tracks the two adjusts separately and authors the
	// per-object previous-world positions in the previous frame's relative space (Lighting.hlsl:176 etc.). We must
	// translate the reconstructed world by the camera displacement (cur - prev adjust) to move it into the previous
	// frame's space before applying prevVP. Without this the reprojection assumes the camera only ROTATED, so motion
	// vectors are wrong under camera TRANSLATION (the sky still looks right because it is at infinity / translation-
	// invariant — which is exactly why the engine's own DeferredCompositeCS sky path can feed the current-relative
	// position to both legs). The translation scales with the homogeneous w, so it composes correctly pre-divide.
	//
	// Jitter: the game unprojects with the JITTERED inverse and reprojects UNJITTERED; that factors into a jitter-free
	// reprojection ∘ a current de-jitter, and SL applies the de-jitter itself via jitterOffset — so both legs here use
	// the UNJITTERED VP. .Transpose() maps the game's mul(M,v) column-vector matrices to SL's row-vector convention.
	Matrix curVP = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	Matrix prevVP = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered().Transpose();
	const auto& posAdj = globals::game::frameBufferCached.GetCameraPosAdjust();
	const auto& prevPosAdj = globals::game::frameBufferCached.GetCameraPreviousPosAdjust();
	Matrix camDelta = Matrix::CreateTranslation(posAdj.x - prevPosAdj.x, posAdj.y - prevPosAdj.y, posAdj.z - prevPosAdj.z);
	Matrix clipToPrevClip = curVP.Invert() * camDelta * prevVP;  // clip -> cur-rel world -> prev-rel world -> prev clip
	Matrix prevClipToClip = clipToPrevClip.Invert();
	a_consts.clipToPrevClip = *reinterpret_cast<const sl::float4x4*>(&clipToPrevClip);
	a_consts.prevClipToClip = *reinterpret_cast<const sl::float4x4*>(&prevClipToClip);

	a_consts.jitterOffset = { -a_jitterX, -a_jitterY };
	// reset: sample-matched history invalidation (its needNewPasses). Skyrim's equivalent
	// discontinuities are loading-screen exits — the camera teleports and every temporal
	// history (DLSS/DLSS-G/FSR) is garbage for the first frame after.
	{
		static bool s_wasLoading = false;
		const bool loading = globals::state->isLoadingMenuOpen;
		a_consts.reset = (!loading && s_wasLoading) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		s_wasLoading = loading;
	}
	a_consts.mvecScale = { 1.0f, 1.0f };
	a_consts.motionVectors3D = sl::Boolean::eFalse;
	a_consts.motionVectorsInvalidValue = FLT_MIN;
	a_consts.orthographicProjection = sl::Boolean::eFalse;
	a_consts.motionVectorsDilated = sl::Boolean::eFalse;
	a_consts.motionVectorsJittered = sl::Boolean::eFalse;

	// Validity gate (see the function comment). Checks one representative of each
	// contamination path: the reprojection pair (NaN from inverting a zero/singular
	// view-proj) and the camera basis (NaN from recalculateCameraMatrices normalizing
	// zero rows). Also rejects a degenerate projection (_33 == 0 => zero cameraData).
	if (!std::isfinite(clipToPrevClip._11) || !std::isfinite(prevClipToClip._11) ||
		!std::isfinite(a_consts.cameraRight.x) || !std::isfinite(a_consts.cameraUp.y) ||
		cameraViewToClip._33 == 0.0f) {
		static uint32_t s_skipN = 0;
		if ((s_skipN++ % 240) == 0)
			logger::debug("[Streamline] skipping constants set - engine camera data not valid this frame");
		return false;
	}

	return true;
}

// Wrap one DXVK interop D3D11 resource as an SL Vulkan resource. SL's VK backend documents the
// sl::Resource view (VkImageView) as MANDATORY (null faults slEvaluateFeature), so create a transient 2D
// view (depth aspect for depth formats); the caller collects a_outView for destruction after the submit.
// Returns false if the backing VkImage can't be obtained.
static bool cs_WrapInteropImage(DxvkInterop* a_dxvk, VkDevice a_device, PFN_vkCreateImageView a_createView,
	ID3D11Resource* a_res, sl::Resource& a_out, uint32_t a_w, uint32_t a_h, VkImageView& a_outView)
{
	a_outView = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	if (!a_dxvk->GetVkImage(a_res, &image, &layout, &info) || image == VK_NULL_HANDLE)
		return false;
	VkImageView view = VK_NULL_HANDLE;
	if (a_createView) {
		VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		ci.image = image;
		ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ci.format = info.format;
		ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		if (info.format == VK_FORMAT_D32_SFLOAT || info.format == VK_FORMAT_D24_UNORM_S8_UINT ||
			info.format == VK_FORMAT_D16_UNORM || info.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
			ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		ci.subresourceRange.levelCount = 1;
		ci.subresourceRange.layerCount = 1;
		a_createView(a_device, &ci, nullptr, &view);
		a_outView = view;
	}
	a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, view, static_cast<uint32_t>(layout) };
	a_out.width = a_w;
	a_out.height = a_h;
	a_out.nativeFormat = static_cast<uint32_t>(info.format);
	a_out.mipLevels = info.mipLevels;
	a_out.arrayLayers = info.arrayLayers;
	a_out.usage = static_cast<uint32_t>(info.usage);
	a_out.flags = static_cast<uint32_t>(info.flags);
	return true;
}

// Destroy the transient views created by cs_WrapInteropImage. Called on every exit path (success, wrap
// failure, and the no-command-buffer bail) so views never leak — EvaluateFSRFrameGen runs every frame.
static void cs_DestroyViews(DxvkInterop* a_dxvk, VkDevice a_device, const VkImageView* a_views, int a_count)
{
	if (auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
			a_dxvk->GetDeviceProcAddr()(a_device, "vkDestroyImageView"))) {
		for (int i = 0; i < a_count; ++i)
			if (a_views[i] != VK_NULL_HANDLE)
				vkDestroyImageView(a_device, a_views[i], nullptr);
	}
}

// Shared evaluate core: sets the frame constants on a_viewport, wraps depth + MV (+ color when BOTH
// colorIn and colorOut are given), tags them, evaluates a_feature into a fresh DXVK frame command buffer,
// submits (waitIdle), and frees the transient views on every path. colorIn/colorOut == null => the
// depth+MV-only FG-prepare. Returns the slEvaluateFeature result (or a pre-eval error code on bail).
// No __try here — each caller wraps the call in its own SEH guard.
static sl::Result cs_EvaluateFeatureCore(sl::Feature a_feature, const sl::ViewportHandle& a_viewport,
	ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY, ID3D11Resource* a_hudlessColor = nullptr)
{
	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk)
		return sl::Result::eErrorNotInitialized;

	// The render frame's explicit-ID token: these constants land on the same frame as the DLSS-G
	// tags and present marker by construction (all fetch with g_sl.renderFrameId).
	sl::FrameToken* token = RenderFrameToken();
	if (!token)
		return sl::Result::eErrorMissingInputParameter;

	// Set constants AND evaluate exactly once per display frame per viewport, and never one without the other.
	// Main_PostProcessing runs more than once per rendered frame:
	//  * A duplicate slSetConstants is rejected by SL ("Setting different 'common' constants multiple times within
	//    the same frame is NOT allowed"), and letting a second evaluate run feeds NGX/FFX the SAME frame twice
	//    (same image, same jitter), corrupting their temporal accumulation — shimmer/crawl/judder on every SL
	//    upscaler and the FSR FG-prepare, while plain TAA is unaffected. First call wins.
	//  * An evaluate WITHOUT this frame's constants is equally wrong: SL's 3-deep ring silently falls back to the
	//    LAST SET constants on an exact-match miss, so the upscaler would de-jitter with the previous frame's
	//    offset. If the camera data is invalid this frame (cs_BuildConstants false), skip the evaluate too and
	//    retry the whole pair next frame.
	// Keyed on renderFrameId (the render frame's explicit ID, render thread only — set once per
	// frame in BeginRenderFrame, so a second Main_PostProcessing pass in the same frame can't slip
	// a duplicate through). Viewports used: 0 (upscale/keep-alive) and 1 (FSR FG-prepare).
	static uint32_t s_evalFrameByVp[2] = { UINT32_MAX, UINT32_MAX };
	static uint32_t s_constFrameByVp[2] = { UINT32_MAX, UINT32_MAX };
	const uint32_t vpId = a_viewport;
	if (vpId < 2) {
		if (s_evalFrameByVp[vpId] == g_sl.renderFrameId)
			return sl::Result::eOk;
		if (s_constFrameByVp[vpId] != g_sl.renderFrameId) {
			sl::Constants consts;
			if (!cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY))
				return sl::Result::eOk;
			s_constFrameByVp[vpId] = g_sl.renderFrameId;
			g_sl.slSetConstants(consts, *token, a_viewport);
		}
		s_evalFrameByVp[vpId] = g_sl.renderFrameId;
	} else {
		sl::Constants consts;
		if (!cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY))
			return sl::Result::eOk;
		g_sl.slSetConstants(consts, *token, a_viewport);
	}

	VkDevice vkDevice = dxvk->GetDevice();
	auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
		dxvk->GetDeviceProcAddr()(vkDevice, "vkCreateImageView"));
	VkImageView views[5] = {};
	int nv = 0;
	const auto wrap = [&](ID3D11Resource* a_res, sl::Resource& a_out, uint32_t a_w, uint32_t a_h) -> bool {
		VkImageView v = VK_NULL_HANDLE;
		if (!cs_WrapInteropImage(dxvk, vkDevice, vkCreateImageView, a_res, a_out, a_w, a_h, v))
			return false;
		if (v != VK_NULL_HANDLE && nv < 5)
			views[nv++] = v;
		return true;
	};

	const bool haveColor = (a_colorIn && a_colorOut);
	const bool haveHudless = (a_hudlessColor != nullptr);
	sl::Resource colorInRes{}, colorOutRes{}, depthRes{}, mvecRes{}, hudlessRes{};
	bool ok = wrap(a_depth, depthRes, a_renderWidth, a_renderHeight) &&
	          wrap(a_motionVectors, mvecRes, a_renderWidth, a_renderHeight);
	if (ok && haveColor)
		ok = wrap(a_colorIn, colorInRes, a_renderWidth, a_renderHeight) &&
		     wrap(a_colorOut, colorOutRes, a_outputWidth, a_outputHeight);
	// HUDLessColor is the post-upscale, pre-UI scene at DISPLAY (output) resolution — tag it at output dims.
	if (ok && haveHudless)
		ok = wrap(a_hudlessColor, hudlessRes, a_outputWidth, a_outputHeight);
	if (!ok) {
		cs_DestroyViews(dxvk, vkDevice, views, nv);
		return sl::Result::eErrorMissingInputParameter;
	}

	// Motion vectors must carry the SAME dimensions + subrect as depth (SL/FFX reconstruct MV on the depth
	// grid). Force MV's reported dimensions to depth's and tag both with the one renderExtent below.
	mvecRes.width = depthRes.width;
	mvecRes.height = depthRes.height;

	sl::Extent renderExtent{ 0, 0, a_renderWidth, a_renderHeight };
	sl::Extent outputExtent{ 0, 0, a_outputWidth, a_outputHeight };
	sl::ResourceTag tags[5];
	uint32_t nt = 0;
	if (haveColor) {
		tags[nt++] = sl::ResourceTag{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
		tags[nt++] = sl::ResourceTag{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent };
	}
	// DLSS-G inputs are eOnlyValidNow: SL snapshots them into its own copies inside this very
	// command buffer at tag time, so present-time interpolation never touches live game
	// resources — the doc-correct lifetime when validity at present cannot be guaranteed, and
	// the reason no CPU-side wait exists anywhere in this path. Ordering of the copies
	// themselves is SL's own §16.0 contract (its blocking present waits the client present
	// semaphore, which DXVK's final blit signals after this queue-ordered submission).
	// HISTORY: eOnlyValidNow alone flashed in the GDI-copy-present era (2026-07-04), but that
	// world also broke the pacer outright; on flip-model presents with the blocking mode
	// working (2026-07-05) this is the documented configuration. The upscaler in/out tags stay
	// eValidUntilPresent: they are consumed inside this same submission and DLSS-G reads the
	// intercepted backbuffer, not these.
	tags[nt++] = sl::ResourceTag{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &renderExtent };
	tags[nt++] = sl::ResourceTag{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &renderExtent };
	if (haveHudless)
		tags[nt++] = sl::ResourceTag{ &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent };

	sl::Result evalRes = sl::Result::eErrorNotInitialized;
	VkCommandBuffer cmd = dxvk->BeginFrameCommandBuffer();
	if (cmd != VK_NULL_HANDLE) {
		g_sl.slSetTagForFrame(*token, a_viewport, tags, nt, cmd);
		const sl::BaseStructure* inputs[] = { &a_viewport };
		evalRes = g_sl.slEvaluateFeature(a_feature, *token, inputs, static_cast<uint32_t>(std::size(inputs)), cmd);
		// Submit async — no per-frame GPU catch-up wait (e22095b9's perf win, kept under frame
		// generation too thanks to the bound above). The views are referenced by the submitted
		// dispatch; the deferred-delete ring frees them once this slot's fence signals.
		dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/false);
		dxvk->QueueViewsForDeferredDelete(views, static_cast<uint32_t>(nv));
	} else {
		// Nothing was submitted — the views carry no pending GPU work, so free them immediately.
		cs_DestroyViews(dxvk, vkDevice, views, nv);
	}
	return evalRes;
}

void Streamline::EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	// Best-effort DLSS super-resolution dispatch on DXVK's Vulkan device.
	//
	// NOTE: this path is NVIDIA-only and cannot be exercised on the current AMD hardware (and ships
	// without the SL plugin DLLs), so it is structurally implemented from the SL VK spec but not
	// runtime-validated. Fully gated (featureDLSS) and SEH-guarded so any fault latches the feature off.
	if (!initialized || !featureDLSS || g_sl.dispatchFaulted)
		return;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	// DXVK itself is a hard requirement (load-time enforced), so the per-frame SL paths below only gate on
	// whether the interop command ring is ready yet — a timing check, not a DXVK-availability check.
	if (!dxvk->CommandResourcesReady())
		return;
	// Flush DXVK's pending D3D11 rendering so the interop VkImages are submitted/consistent before SL
	// records its compute work; SL applies the layout barriers itself from the tagged per-resource state.
	dxvk->FlushRenderingCommands();

	(void)a_sharpness;  // sharpness is deprecated in DLSSOptions; RCAS handles sharpening.

	__try {
		// Map quality preset -> DLSS mode (shared convention with getUpscaleRatio: 0=Native,1=Quality,
		// 2=Balanced,3=Performance,4=Ultra Performance) so the render res stays in the mode's [min,max].
		sl::DLSSMode dlssMode = sl::DLSSMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			dlssMode = sl::DLSSMode::eDLAA;  // Native AA, no upscale (render ratio 1.0)
			break;
		case 1:
			dlssMode = sl::DLSSMode::eMaxQuality;  // DLSS Quality (0.667x)
			break;
		case 2:
			dlssMode = sl::DLSSMode::eBalanced;  // DLSS Balanced (0.58x)
			break;
		case 3:
			dlssMode = sl::DLSSMode::eMaxPerformance;  // DLSS Performance (0.5x)
			break;
		case 4:
			dlssMode = sl::DLSSMode::eUltraPerformance;  // DLSS Ultra Performance (0.33x)
			break;
		default:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		}

		sl::DLSSOptions options{};
		options.mode = dlssMode;
		options.outputWidth = a_outputWidth;
		options.outputHeight = a_outputHeight;
		options.colorBuffersHDR = sl::Boolean::eFalse;  // CS upscales the SDR scene; HDR composite happens later.
		options.useAutoExposure = sl::Boolean::eTrue;   // matches the proven dev-branch DLSS integration.

		// Per-GPU DLSS render-preset selection (ported from the proven dev-branch integration). RTX 40+
		// (Ada) uses preset M for the upscaling modes; RTX 20/30 (Turing/Ampere) use preset J. Resolved
		// once at device-set time from the Vulkan physical device (isRTXBelow40Series / isNvidiaGPU).
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

		if (g_sl.slDLSSSetOptions(g_sl.viewport, options) != sl::Result::eOk)
			return;

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureDLSS, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);

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
		logger::error("[Streamline] DLSS dispatch faulted — disabling for this session");
	}
}

void Streamline::EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	if (!initialized || !featureXeSS || g_sl.dispatchFaulted)
		return;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return;
	dxvk->FlushRenderingCommands();

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
		xessOpts.colorBuffersHDR = sl::Boolean::eFalse;
		if (g_sl.slXeSSSetOptions(g_sl.viewport, xessOpts) != sl::Result::eOk)
			return;

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureXeSS, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);

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
		logger::error("[Streamline] XeSS dispatch faulted — disabling for this session");
	}
}

void Streamline::EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	if (!initialized || !featureFSR || g_sl.dispatchFaulted)
		return;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return;
	dxvk->FlushRenderingCommands();

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
		fsrOpts.colorBuffersHDR = sl::Boolean::eFalse;
		if (g_sl.slFSRSetOptions(g_sl.viewport, fsrOpts) != sl::Result::eOk)
			return;

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureFSR, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);

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
		logger::error("[Streamline] FSR dispatch faulted — disabling for this session");
	}
}

void Streamline::EvaluateFSRFrameGen(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	// Drives the sl.fsr plugin's standalone FG-prepare. Tags depth + MV but NO color, so the plugin's
	// shared kFeatureFSR evaluate runs only the FrameGenerationPrepare (not an upscale) — making FSR FG
	// independent of the active upscaler. The present-time FFX swapchain consumes what this prepares.
	//
	// Runs on a DEDICATED viewport (1), separate from the upscaler's viewport (0). SL keys both common
	// constants AND tagged resources by (frameToken, viewport): on viewport 0 this would (a) collide with
	// the upscaler's constants (eErrorDuplicatedConstants + clobber) and (b) see the upscaler's leftover
	// COLOR tags, making the plugin's shared kFeatureFSR evaluate take the UPSCALE branch instead of
	// FG-prepare. dlss_g also reads viewport-0 constants at present, so an isolated viewport keeps all
	// three apart. The plugin's FG-prepare uses GLOBAL ctx (fgContext/fgWrappedSwapchain/fgEnabled), not
	// per-viewport options, so viewport 1 is safe; it only needs its own constants + depth/MV tags.
	if (!initialized || !featureFSRFG || g_sl.dispatchFaulted)
		return;
	if (!a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return;
	dxvk->FlushRenderingCommands();

	__try {
		const sl::ViewportHandle fgViewport{ 1 };
		// Tag depth + MV (FG-prepare inputs) AND the hudless scene (present-time UI extraction). This drives
		// the dedicated FSR FRAME-GEN feature (sl.fsr_g / kFeatureFSR_G) — its evaluate runs FG-prepare.
		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureFSR_G, fgViewport,
			nullptr, nullptr, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY, a_hudlessColor);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		if (evalRes != s_loggedRes) {
			s_loggedRes = evalRes;
			logger::info("[Streamline] FSR FG-prepare result={} render={}x{}", static_cast<int>(evalRes), a_renderWidth, a_renderHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR FG-prepare faulted — disabling for this session");
	}
}

void Streamline::SetDLSSGMode(bool a_enable, uint32_t a_displayWidth, uint32_t a_displayHeight,
	uint32_t a_numFramesToGenerate, bool a_autoMode, bool a_dynamic, float a_dynamicTargetFps)
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;

	// DLSS-G may be runtime-unloaded when not the selected FG method (guide §18). Don't call its options
	// entry point while unloaded — wait for the load (driven by the swapchain recreate) to land.
	if (!g_dlssgCurrentlyLoaded)
		return;

	// Clamp the requested multiplier to what the hardware reports (numFramesToGenerateMax). 0 = not yet
	// queried — pass the request through and let SL clamp it. Always at least 1 (2x single-frame).
	const uint32_t maxFrames = g_sl.dlssgMaxFramesToGenerate.load(std::memory_order_acquire);
	uint32_t numFrames = a_numFramesToGenerate < 1u ? 1u : a_numFramesToGenerate;
	if (maxFrames > 0u && numFrames > maxFrames)
		numFrames = maxFrames;

	// Sample-matched cadence: slDLSSGSetOptions is (re)issued EVERY frame (the Streamline_Sample
	// calls SetDLSSGOptions unconditionally in its render loop; SL treats redundant sets as
	// cheap no-ops). The cached fields below are kept only to log on real changes. Render dims are
	// deliberately NOT part of the options or this key (see the extent note below), so an upscaler
	// quality/preset change produces bit-identical options — invisible to DLSS-G, like the sample.
	const bool changed = !(g_sl.dlssgModeCached && g_sl.dlssgModeOn == a_enable &&
		g_sl.dlssgCachedNumFrames == numFrames && g_sl.dlssgCachedAuto == a_autoMode &&
		g_sl.dlssgCachedDynamic == a_dynamic && g_sl.dlssgCachedDynamicFps == a_dynamicTargetFps &&
		g_sl.dlssgCachedDisplayW == a_displayWidth && g_sl.dlssgCachedDisplayH == a_displayHeight);

	// DXVK blocking-mode support (eBlockPresentingClientQueue): while DLSS-G runs, SL blocks
	// inside the present call, and DXVK's frame-latency wait would deadlock against it (its
	// signal fires on the thread SL parks). The flag also gates the presenter's blocking-mode
	// behaviors (VkPresentIdKHR under FG ownership, MAILBOX preference).
	{
		static void (*s_setSkip)(uint32_t) = nullptr;
		static bool s_resolved = false;
		if (!s_resolved) {
			s_resolved = true;
			if (HMODULE m = GetModuleHandleW(L"dxvk_d3d11.dll"))
				s_setSkip = reinterpret_cast<void (*)(uint32_t)>(GetProcAddress(m, "dxvkSetSkipFrameLatencySync"));
			if (!s_setSkip)
				logger::warn("[Streamline] dxvkSetSkipFrameLatencySync not found - DLSS-G blocking mode may deadlock");
		}
		if (s_setSkip)
			s_setSkip(a_enable ? 1u : 0u);
	}

	__try {
		sl::DLSSGOptions options{};
		// eDynamic (Dynamic MFG) overrides eAuto; both fall back from the caller when unsupported.
		options.mode = !a_enable ? sl::DLSSGMode::eOff :
		               a_dynamic ? sl::DLSSGMode::eDynamic :
		               a_autoMode ? sl::DLSSGMode::eAuto :
		                            sl::DLSSGMode::eOn;
		options.numFramesToGenerate = numFrames;
		// Dynamic mode targets a frame rate (0 => SL auto-detects the monitor refresh); numFramesToGenerate
		// is ignored by eDynamic per the SL guide.
		if (a_dynamic)
			options.dynamicTargetFrameRate = a_dynamicTargetFps;
		// eRetainResourcesWhenOff: DLSS-G is toggled off every loading screen / menu and back on for
		// gameplay; retaining its resources across those off periods avoids realloc stutter on re-enable.
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		// NO eDynamicResolutionEnabled and NO dynamicResWidth/Height — sample-exact. The Streamline_Sample
		// sets that flag ONLY in its true dynamic-resolution mode (per-frame varying render size); for fixed
		// quality presets — even with the upscaler rendering below display res — its DLSSGOptions carry no
		// render dims at all, and the per-frame sl::Extent on the mvec/depth tags describes the render
		// sub-rect. CS has no dynamic-res mode (a quality change is a discrete re-init, not DRS), so the
		// options must never vary with render size: keying/reissuing options on render dims made a simple
		// quality-slider change reset DLSS-G's pacer mid-present — the "changed the preset and it froze"
		// wedge. (Setting the DRS flag with dynamicRes == color also device-loses during interpolation.)
		options.mvecDepthWidth = a_displayWidth;  // texture dims, not render size (extent gives the sub-rect)
		options.mvecDepthHeight = a_displayHeight;  // texture dims, not render size (extent gives the sub-rect)
		options.colorWidth = a_displayWidth;
		options.colorHeight = a_displayHeight;
		// eBlockNoClientQueues (Vulkan-only; DLSS-G guide §17). SL does NOT block the presenting queue —
		// it runs the frame-gen workload on a non-presenting queue in parallel. Unlike eBlockPresentingClientQueue
		// (which does a blocking hardware present that REQUIRES flip-model and wedges its pacer in
		// NtDxgkSubmitPresentToHwQueue once alt-tab occludes the window onto the GDI-copy path — the aggressive
		// alt-tab freeze), this mode survives the occluded transition. The contract (§15.1): the client must wait
		// on the plugin's inputsProcessingCompletionFence before overwriting the eValidUntilPresent inputs (motion
		// vectors, HUDless) — handled by CaptureDLSSGInputFence (present) + WaitDLSSGInputFence (frame start).
		// Depth is eOnlyValidNow (snapshotted) so it needs no wait. The present hook also skips presenting entirely
		// while the window is occluded/minimized (Streamline-sample parity), so the pacer never touches the
		// occluded surface at all.
		options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockNoClientQueues;
		const sl::Result res = g_sl.slDLSSGSetOptions(g_sl.viewport, options);
		if (res != sl::Result::eOk) {
			if (changed)
				logger::warn("[Streamline] slDLSSGSetOptions failed (result {})", static_cast<int>(res));
		} else {
			g_sl.dlssgModeCached = true;
			g_sl.dlssgModeOn = a_enable;
			g_sl.dlssgCachedNumFrames = numFrames;
			g_sl.dlssgCachedAuto = a_autoMode;
			g_sl.dlssgCachedDynamic = a_dynamic;
			g_sl.dlssgCachedDynamicFps = a_dynamicTargetFps;
			g_sl.dlssgCachedDisplayW = a_displayWidth;
			g_sl.dlssgCachedDisplayH = a_displayHeight;
			if (changed)
				logger::info("[Streamline] DLSS-G mode={} ({}) numFrames={} targetFps={} (max {}) display={}x{}", a_enable,
					!a_enable ? "off" : a_dynamic ? "dynamic" : a_autoMode ? "auto" : "on", numFrames, a_dynamicTargetFps, maxFrames,
					a_displayWidth, a_displayHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G SetOptions faulted — disabling for this session");
	}
}

bool Streamline::SetFSRFrameGen(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight, bool a_hdr,
	bool a_debugView, bool a_debugTearLines, bool a_debugPacingLines, bool a_onlyPresentGenerated)
{
	// Returns true only when the option was actually delivered to the plugin. The caller retries until
	// it does, because featureFSRFG (and the FG entry points) come up a few frames AFTER the first
	// CheckResources — a one-shot transition would silently miss that window.
	if (!initialized || !featureFSRFG || !g_sl.slFSRFrameGenerationSetOptions || g_sl.dispatchFaulted)
		return false;
	// sl.fsr_g may be runtime-unloaded when DLSS-G is the selected method (twin of SetDLSSGMode's guard):
	// don't call its options entry point while unloaded.
	if (!g_fsrfgCurrentlyLoaded)
		return false;

	bool ok = false;
	__try {
		sl::FSRFrameGenOptions options{};
		options.enabled = a_enable ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.renderWidth = a_renderWidth;
		options.renderHeight = a_renderHeight;
		options.displayWidth = a_displayWidth;
		options.displayHeight = a_displayHeight;
		options.colorBuffersHDR = a_hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugView = a_debugView ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugTearLines = a_debugTearLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugPacingLines = a_debugPacingLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.onlyPresentGenerated = a_onlyPresentGenerated ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		const sl::Result res = g_sl.slFSRFrameGenerationSetOptions(g_sl.viewport, options);
		if (res != sl::Result::eOk) {
			logger::warn("[Streamline] slFSRFrameGenerationSetOptions failed (result {})", static_cast<int>(res));
		} else {
			ok = true;
			logger::info("[Streamline] FSR frame generation {} (render {}x{} display {}x{})",
				a_enable ? "ENABLED" : "disabled", a_renderWidth, a_renderHeight, a_displayWidth, a_displayHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR SetFrameGen faulted — disabling for this session");
	}
	return ok;
}

void Streamline::LogFSRFrameGenStats()
{
	// Throttled (~once/2s) FSR-FG activity log. numFramesActuallyPresented is the sl.fsr
	// plugin's own per-present measurement: 2 = interpolation active (doubling), 1 = the
	// FFX swapchain is absent or passing through. The one reliable runtime signal for
	// whether FSR-FG is actually generating frames (screen captures can't see it - the
	// FFX overlay/present happens after CS's capture point and DWM may not composite it).
	if (!initialized || !featureFSRFG || !g_sl.slFSRGetFrameGenState || g_sl.dispatchFaulted)
		return;
	static uint32_t s_n = 0;
	if ((s_n++ % 120) != 0)
		return;
	__try {
		sl::FSRFrameGenState state{};
		if (g_sl.slFSRGetFrameGenState(g_sl.viewport, state) == sl::Result::eOk)
			logger::info("[Streamline] FSR-FG state: framesPresented={} status={} vram={} MB",
				state.numFramesActuallyPresented, state.status,
				state.estimatedVRAMUsageInBytes >> 20);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::QueryDLSSGCapabilities()
{
	// slDLSSGGetState must run on the present thread (SL requirement) — CS calls this from its present hook.
	// We only need the static capability (numFramesToGenerateMax), so query once and cache it.
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted || !g_dlssgCurrentlyLoaded)
		return;
	if (g_sl.dlssgMaxFramesToGenerate.load(std::memory_order_acquire) != 0u)
		return;  // already cached
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

bool Streamline::IsDLSSGDynamicSupported() const
{
	return g_sl.dlssgDynamicSupported.load(std::memory_order_acquire);
}

void Streamline::LogReflexStatus()
{
	// Throttled (~once/2s) Reflex-status log. We deliberately do NOT call slDLSSGGetState here: the SL header
	// requires it be called on the PRESENT thread (CS would call it on the render thread → SL logs "slDLSSGGetState
	// must be synchronized with the present thread" every call), and numFramesActuallyPresented is an unreliable,
	// thread-timing-dependent value anyway (verify doubling visually / with PresentMon). lowLatencyAvailable is the
	// reliable "Reflex is working" signal and slReflexGetState is not present-thread-restricted.
	if (g_sl.dispatchFaulted || !g_sl.slReflexGetState || !g_sl.dlssgModeOn)
		return;
	static uint32_t s_n = 0;
	if ((s_n++ % 120) != 0)
		return;
	__try {
		sl::ReflexState rstate{};
		if (g_sl.slReflexGetState(rstate) == sl::Result::eOk)
			logger::info("[Streamline] Reflex lowLatencyAvailable={} latencyReportAvailable={} flashDriverControlled={}",
				rstate.lowLatencyAvailable, rstate.latencyReportAvailable, rstate.flashIndicatorDriverControlled);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::TagDLSSGResources(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight)
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;
	if (!a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())
		return;

	__try {
		// Input protection (DLSS-G guide §16.1, eBlockNoClientQueues): MV is tagged as the LIVE
		// kMOTION_VECTOR, sample-style (the former 3-deep CS-side MV ring was removed). NOTE: the
		// present-hook bound that guaranteed the GPU executed this frame's evaluate/tag work before
		// the present was REMOVED, so live-MV tagging no longer has that ordering backing it.
		// DEPTH stays eOnlyValidNow because the in-place depth upscale rewrites the tagged
		// kMAIN_COPY before present regardless of any bound.

		// Tag DLSS-G inputs for the SAME frame the constants/markers/present use, else SL cannot match
		// them to the presented frame and drops interpolation (explicit render-frame token).
		sl::FrameToken* token = RenderFrameToken();
		if (!token)
			return;

		// SL's Vulkan backend needs a VkImageView per tagged resource (the DLSS-SR path proved a
		// null view faults SL). DLSS-G consumes these at present time after a non-blocking submit,
		// so views are cached per VkImage (recreated only on change) rather than created+destroyed
		// per frame — see g_sl.dlssgViewCache. slot: 0=depth, 1=motion, 2=hudless.
		VkDevice vkDevice = dxvk->GetDevice();
		auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
			dxvk->GetDeviceProcAddr()(vkDevice, "vkCreateImageView"));
		auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
			dxvk->GetDeviceProcAddr()(vkDevice, "vkDestroyImageView"));

		const auto cachedView = [&](int a_slot, VkImage a_image, VkFormat a_format) -> VkImageView {
			auto* entries = g_sl.dlssgViewCache[a_slot];
			// Hit: this image already has a live view (steady state for every frame after the first few).
			for (int i = 0; i < 4; ++i)
				if (entries[i].image == a_image && entries[i].view != VK_NULL_HANDLE)
					return entries[i].view;
			// Miss: take an empty entry, else evict round-robin (only reached when engine targets were
			// recreated — the caller drained the interop ring first, so the evicted view is not in flight).
			int idx = -1;
			for (int i = 0; i < 4; ++i)
				if (entries[i].view == VK_NULL_HANDLE) {
					idx = i;
					break;
				}
			if (idx < 0) {
				idx = g_sl.dlssgViewEvict[a_slot];
				g_sl.dlssgViewEvict[a_slot] = (g_sl.dlssgViewEvict[a_slot] + 1) % 4;
				if (entries[idx].view != VK_NULL_HANDLE && vkDestroyImageView)
					vkDestroyImageView(vkDevice, entries[idx].view, nullptr);
			}
			auto& c = entries[idx];
			c.view = VK_NULL_HANDLE;
			c.image = a_image;
			if (vkCreateImageView) {
				VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				ci.image = a_image;
				ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				ci.format = a_format;
				ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				if (a_format == VK_FORMAT_D32_SFLOAT || a_format == VK_FORMAT_D24_UNORM_S8_UINT ||
					a_format == VK_FORMAT_D16_UNORM || a_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
					ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				ci.subresourceRange.levelCount = 1;
				ci.subresourceRange.layerCount = 1;
				vkCreateImageView(vkDevice, &ci, nullptr, &c.view);
			}
			return c.view;
		};

		const auto makeResource = [&](ID3D11Resource* a_res, sl::Resource& a_out, int a_slot) {
			VkImage image = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			if (!dxvk->GetVkImage(a_res, &image, &layout, &info) || image == VK_NULL_HANDLE)
				return false;
			a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, cachedView(a_slot, image, info.format), static_cast<uint32_t>(layout) };
			// Resource dimensions = FULL texture size (the VkImage); the valid render sub-rect is the tag Extent.
			// Reporting the render size here told SL/DLSS-G the buffer was render-sized while the VkImage is
			// full-sized -> mis-scaled inputs. info.extent is the real texture size.
			a_out.width = info.extent.width;
			a_out.height = info.extent.height;
			a_out.nativeFormat = static_cast<uint32_t>(info.format);
			a_out.mipLevels = info.mipLevels;
			a_out.arrayLayers = info.arrayLayers;
			a_out.usage = static_cast<uint32_t>(info.usage);
			a_out.flags = static_cast<uint32_t>(info.flags);
			return true;
		};

		sl::Resource depthRes{}, mvecRes{};
		if (!makeResource(a_depth, depthRes, 0) ||
			!makeResource(a_motionVectors, mvecRes, 1))
			return;

		// Motion vectors must carry the SAME dimensions + subrect as depth (DLSS-G dilates MV on the depth
		// grid — DLSSG.MVecsSubrect must equal DLSSG.DepthSubrect). Force MV's dimensions to depth's; both are
		// tagged with the one `extent` below.
		mvecRes.width = depthRes.width;
		mvecRes.height = depthRes.height;

		sl::Extent extent{};
		extent.width = a_renderWidth;
		extent.height = a_renderHeight;

		// Depth: eOnlyValidNow — SL snapshots the depth into its own copy at tag time. This replaces a CS-side
		// CopyResource: the tagged depth is kMAIN_COPY, the render-res depth the engine saved before the in-place
		// depth upscale rewrote kMAIN; eValidUntilPresent would have SL read it back at present, after that upscale
		// and the next frame have reused kMAIN_COPY. eOnlyValidNow captures it now, while it is still correct.
		// MV: eValidUntilPresent (referenced, no SL copy) but pointing at the caller's private ring slot, which
		// is valid by construction until well past this present's generation read (see the §16.1 note above).
		sl::ResourceTag tags[3];
		uint32_t tagCount = 0;
		tags[tagCount++] = { &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &extent };
		tags[tagCount++] = { &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extent };

		// HUDLessColor is the post-upscale, pre-UI scene at DISPLAY (back-buffer) resolution — NOT render
		// resolution like depth/MV. Tag it with the display extent so DLSS-G's UI extraction samples the
		// right region (tagging it render-res left it sampling only the top-left render sub-rect).
		sl::Extent displayExtent{};
		displayExtent.width = a_displayWidth;
		displayExtent.height = a_displayHeight;
		sl::Resource hudlessRes{};
		if (a_hudlessColor && makeResource(a_hudlessColor, hudlessRes, 2))
			tags[tagCount++] = { &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &displayExtent };
		else
			// Capture/wrap failed (early frame, SRV not ready, resolution change). Tag a NULL hudless so DLSS-G
			// does no UI extraction THIS frame instead of retaining the previous eValidUntilPresent hudless (a
			// stale UI region composited into the interpolated frame).
			tags[tagCount++] = { nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, nullptr };

		VkCommandBuffer cmd = dxvk->BeginFrameCommandBuffer();
		if (cmd == VK_NULL_HANDLE)
			return;

		g_sl.slSetTagForFrame(*token, g_sl.viewport, tags, tagCount, cmd);
		dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/false);
		g_sl.dlssgTaggedThisFrame = true;  // valid inputs tagged: present will interpolate this frame
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G tag faulted — disabling for this session");
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

		// Null-resource tags tell DLSS-G "no valid interpolation inputs this frame" so it presents the
		// real frame 1:1. Required on every present that did not set valid tags (the first present, and
		// loading/paused/menu frames) — otherwise SL's present hook waits on inputs that never arrive and
		// stalls. Null tags carry no GPU work, so they are set with a NULL command buffer: this works
		// even before DXVK's interop command ring is ready (the very first present at swapchain create).
		sl::ResourceTag tags[] = {
			sl::ResourceTag{ nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
		};
		// Use a real command buffer when the interop ring is ready (most frames); fall back to a null
		// command buffer at the very first present (before the ring exists) — null tags record no work.
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		auto* dxvk = DxvkInterop::GetSingleton();
		if (dxvk->CommandResourcesReady())
			cmd = dxvk->BeginFrameCommandBuffer();
		g_sl.slSetTagForFrame(*token, g_sl.viewport, tags, static_cast<uint32_t>(std::size(tags)), cmd);
		if (cmd != VK_NULL_HANDLE)
			dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/false);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G clear-tags faulted — disabling for this session");
	}
}

void Streamline::EnsureDLSSGPresentTag()
{
	// Called from the present path for the DLSS-G/SL-owned swapchain. If the render pass already set a
	// valid tag this frame (gameplay), interpolate; otherwise set a null/passthrough tag so the present
	// never waits on absent inputs. This is what makes the FIRST present (before any render pass) and
	// every loading/menu present safe.
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;
	if (g_sl.dlssgTaggedThisFrame)
		return;
	ClearDLSSGTags();
}

void Streamline::RegisterDxvkOwnershipPredicate()
{
	// Under full interposition SL's interposed present is unconditionally on the present path, so EVERY
	// DXVK swapchain is externally paced. Register the ownership predicate so DXVK omits its present-fence
	// pNext + present-wait worker (which SL never signals — without this the first post-present acquire
	// blocks forever and the game freezes on the intro logo).
	HMODULE dxvkModule = GetModuleHandleW(L"dxvk_d3d11.dll");
	if (!dxvkModule) {
		logger::warn("[Streamline] DXVK module not loaded — cannot register ownership predicate");
		return;
	}
	using SetQueryFn = void (*)(bool (*)(VkSwapchainKHR));
	auto setQuery = reinterpret_cast<SetQueryFn>(GetProcAddress(dxvkModule, "dxvkSetFrameGenOwnershipQuery"));
	if (!setQuery) {
		logger::warn("[Streamline] dxvkSetFrameGenOwnershipQuery not found in DXVK module");
		return;
	}
	setQuery([](VkSwapchainKHR) -> bool { return true; });
	logger::info("[Streamline] registered DXVK ownership predicate (all swapchains externally paced)");

	// Register the swapchain-torn-down callback used to (un)load DLSS-G in the no-swapchain window
	// (Streamline DLSS-G guide §18). Non-fatal if the DXVK build predates the export.
	using SetTornDownFn = void (*)(void (*)());
	if (auto setTornDown = reinterpret_cast<SetTornDownFn>(GetProcAddress(dxvkModule, "dxvkSetSwapchainTornDownCallback"))) {
		setTornDown(&DxvkSwapchainTornDownCallback);
		logger::info("[Streamline] registered DXVK swapchain-torn-down callback (DLSS-G load/unload)");
	} else {
		logger::warn("[Streamline] dxvkSetSwapchainTornDownCallback not found — DLSS-G stays resident when disabled");
	}

	// Present-marker bridge (PresentStart/End at the real vkQueuePresentKHR on DXVK's submit
	// thread): implemented end-to-end (CS_DxvkPresentMarkerBridge + dxvk exports @104/@105) and
	// verified registered in-game, but DELIBERATELY DORMANT. It was built as an async-evaluate
	// enabler and did not fix the DLSS-G world-flash (see the waitForDlssg note in
	// cs_EvaluateFeatureCore); the shipped configuration is the user-validated one with the
	// markers on the render thread. Env-gate CS_SL_PRESENT_MARKER_BRIDGE=1 to experiment.
	if (char v[2] = {}; GetEnvironmentVariableA("CS_SL_PRESENT_MARKER_BRIDGE", v, sizeof(v)) && v[0] == '1') {
		using SetPresentMarkerCbFn = void (*)(void (*)(uint64_t, uint32_t));
		auto setMarkerCb = reinterpret_cast<SetPresentMarkerCbFn>(GetProcAddress(dxvkModule, "dxvkSetPresentMarkerCallback"));
		auto pushFrameId = reinterpret_cast<void (*)(uint64_t)>(GetProcAddress(dxvkModule, "dxvkPushPresentAppFrameId"));
		if (setMarkerCb && pushFrameId) {
			setMarkerCb(&CS_DxvkPresentMarkerBridge);
			g_sl.dxvkPushPresentAppFrameId = pushFrameId;
			logger::info("[Streamline] registered DXVK present-marker bridge (markers at the real present)");
		} else {
			logger::warn("[Streamline] DXVK present-marker bridge exports not found — present markers stay on the render thread");
		}
	}
}

bool Streamline::PresentMarkersBridged() const
{
	return g_sl.dxvkPushPresentAppFrameId != nullptr;
}

void Streamline::NotifyPresentQueued()
{
	// Render thread, at the D3D11 Present call: queue this frame's id for the bridge. DXVK's
	// submit thread pops it when the matching present actually executes.
	if (g_sl.dxvkPushPresentAppFrameId)
		g_sl.dxvkPushPresentAppFrameId(g_sl.renderFrameId);
}

void Streamline::SetDLSSGDesiredLoaded(bool a_loaded)
{
	// Request DLSS-G to be (un)loaded on the next swapchain recreate (applied by DxvkSwapchainTornDownCallback
	// in the no-swapchain window). The caller must follow this with RequestDxvkSwapchainRecreate().
	g_dlssgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool Streamline::IsDLSSGLoaded() const
{
	return g_dlssgCurrentlyLoaded;
}

bool Streamline::IsDLSSGLoadSettled() const
{
	// True once the load/unload reconcile has landed: the desired state has been
	// applied by DxvkSwapchainTornDownCallback and no recreate is outstanding for
	// it. Used to defer the first slDLSSGSetOptions(on) until the FINAL swapchain
	// exists, so a toggle engages FG exactly once instead of engage -> teardown ->
	// re-engage (the visible debug-overlay bounce).
	return g_dlssgDesiredLoaded.load(std::memory_order_acquire) == g_dlssgCurrentlyLoaded;
}

void Streamline::SetFSRFGDesiredLoaded(bool a_loaded)
{
	// Request sl.fsr_g to be (un)loaded on the next swapchain recreate (applied by
	// DxvkSwapchainTornDownCallback in the no-swapchain window). Twin of SetDLSSGDesiredLoaded — the
	// caller must follow this with RequestDxvkSwapchainRecreate().
	g_fsrfgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool Streamline::IsFSRFGLoaded() const
{
	return g_fsrfgCurrentlyLoaded;
}

bool Streamline::IsFSRFGLoadSettled() const
{
	return g_fsrfgDesiredLoaded.load(std::memory_order_acquire) == g_fsrfgCurrentlyLoaded;
}

void Streamline::RequestDxvkSwapchainRecreate(const char* a_reason)
{
	// Force DXVK to recreate its Vulkan swapchain on the next acquire. This is the only window in which
	// sl.dlss_g may be (un)loaded (DxvkSwapchainTornDownCallback runs between destroy and create) and the
	// only way to evict sl.dlss_g's sticky present proxy (it bypasses the Vulkan present hooks, so FSR can
	// never return VK_SUBOPTIMAL to reclaim presentation). Goes through DXVK's own internal recreate,
	// which preserves the D3D11 back buffers.
	static auto requestRecreate = []() -> void (*)() {
		HMODULE dxvkModule = GetModuleHandleW(L"dxvk_d3d11.dll");
		if (!dxvkModule)
			return nullptr;
		return reinterpret_cast<void (*)()>(GetProcAddress(dxvkModule, "dxvkRequestSwapchainRecreate"));
	}();
	if (requestRecreate) {
		requestRecreate();
		logger::info("[Streamline] requested DXVK swapchain recreate ({})", a_reason);
	} else {
		logger::warn("[Streamline] dxvkRequestSwapchainRecreate not found — {} cannot take effect", a_reason);
	}
}
