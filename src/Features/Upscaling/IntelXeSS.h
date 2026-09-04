#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <Windows.h>
#include <winrt/base.h>

#include <xess/xess_d3d11.h>

/**
 * @brief Owns the dynamically loaded Intel XeSS-SR D3D11 backend.
 *
 * The D3D11 XeSS-SR implementation is available only on supported Intel Arc
 * hardware. The DLL is optional: load, device-support, and initialization
 * failures leave the backend disabled without affecting the other upscalers.
 * XeSS is not thread safe, so all methods except the SDK logging callback must be
 * serialized. The engine already provides that, but it moves the calls between the
 * render and main threads, so there is no fixed owning thread; see AdoptCallingThread.
 */
class IntelXeSS
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\XeSS";
	static constexpr const wchar_t* DllName = L"libxess_dx11.dll";

	struct InputResolutionRange
	{
		xess_2d_t optimal{};
		xess_2d_t minimum{};
		xess_2d_t maximum{};
	};

	IntelXeSS() = default;
	~IntelXeSS();

	IntelXeSS(const IntelXeSS&) = delete;
	IntelXeSS& operator=(const IntelXeSS&) = delete;
	IntelXeSS(IntelXeSS&&) = delete;
	IntelXeSS& operator=(IntelXeSS&&) = delete;

	/** @brief True after an attempt has been made to load the optional XeSS DLL. */
	bool triedLoad = false;
	/** @brief True after XeSS successfully creates a context for the D3D11 device. */
	bool available = false;
	/** @brief True after the context has been initialized for an output configuration. */
	bool initialized = false;
	/** @brief True when the runtime accepts the device but recommends a newer graphics driver. */
	bool oldDriverWarning = false;

	/** DLL file-version information for diagnostics in the feature UI. */
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	/** @brief Loads libxess_dx11.dll and resolves the required API entry points. */
	bool Load();
	[[nodiscard]] bool IsRuntimeAvailable() const { return module_ != nullptr; }

	/**
	 * @brief Binds the D3D11 device. The context itself is created on first use so that it,
	 *        and every later SDK call, lives on the render thread as the SDK requires.
	 * @return False for a missing DLL or incomplete API; hardware support is discovered on first use.
	 */
	bool Initialize(ID3D11Device* a_device);

	/** @brief Loads, probes, and creates XeSS resources in one call. */
	bool Initialize(
		ID3D11Device* a_device,
		const xess_2d_t& a_outputResolution,
		xess_quality_settings_t a_quality,
		bool a_useResponsiveMask = true,
		bool a_useAutoExposure = true);

	/**
	 * @brief Initializes or reinitializes XeSS for an output resolution and quality.
	 *
	 * The caller must ensure prior XeSS GPU work is complete before reinitializing.
	 * Low-resolution motion vectors and Skyrim's standard depth are used.
	 */
	bool CreateResources(
		const xess_2d_t& a_outputResolution,
		xess_quality_settings_t a_quality,
		bool a_useResponsiveMask = true,
		bool a_useAutoExposure = true);

	/** @brief Queries XeSS rather than hardcoding a quality-to-resolution ratio. */
	bool QueryOptimalInputResolution(
		const xess_2d_t& a_outputResolution,
		xess_quality_settings_t a_quality,
		InputResolutionRange& a_result);

	/**
	 * @brief Enqueues one native D3D11 XeSS-SR dispatch.
	 *
	 * Motion vectors must be undilated, jitter-free, current-to-previous vectors
	 * in normalized viewport units. They are converted to pixels using the actual
	 * input dimensions. Input and output color resources must not alias.
	 */
	bool Upscale(
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
		float a_exposureScale = 1.0f);

	/** @brief Destroys the XeSS context after the caller has completed pending GPU work. */
	bool DestroyResources();
	/** @brief Destroys the context and unloads the optional XeSS DLL. */
	bool Shutdown();

	[[nodiscard]] const InputResolutionRange& GetInputResolutionRange() const { return inputResolutionRange_; }
	[[nodiscard]] const xess_2d_t& GetOutputResolution() const { return outputResolution_; }
	[[nodiscard]] xess_quality_settings_t GetQuality() const { return quality_; }
	[[nodiscard]] bool UsesResponsiveMask() const { return useResponsiveMask_; }
	[[nodiscard]] bool UsesAutoExposure() const { return useAutoExposure_; }
	[[nodiscard]] const xess_version_t& GetSDKVersion() const { return sdkVersion_; }
	[[nodiscard]] const xess_version_t& GetIntelXeFXVersion() const { return intelXeFXVersion_; }

	[[nodiscard]] static const char* ResultToString(xess_result_t a_result);
	[[nodiscard]] static const char* QualityToString(xess_quality_settings_t a_quality);

private:
	using GetVersionFn = decltype(&xessGetVersion);
	using GetIntelXeFXVersionFn = decltype(&xessGetIntelXeFXVersion);
	using CreateContextFn = decltype(&xessD3D11CreateContext);
	using InitializeContextFn = decltype(&xessD3D11Init);
	using ExecuteFn = decltype(&xessD3D11Execute);
	using GetOptimalInputResolutionFn = decltype(&xessGetOptimalInputResolution);
	using SetVelocityScaleFn = decltype(&xessSetVelocityScale);
	using SetLoggingCallbackFn = decltype(&xessSetLoggingCallback);
	using IsOptimalDriverFn = decltype(&xessIsOptimalDriver);
	using DestroyContextFn = decltype(&xessDestroyContext);

	struct Api
	{
		GetVersionFn getVersion = nullptr;
		GetIntelXeFXVersionFn getIntelXeFXVersion = nullptr;
		CreateContextFn createContext = nullptr;
		InitializeContextFn initializeContext = nullptr;
		ExecuteFn execute = nullptr;
		GetOptimalInputResolutionFn getOptimalInputResolution = nullptr;
		SetVelocityScaleFn setVelocityScale = nullptr;
		SetLoggingCallbackFn setLoggingCallback = nullptr;
		IsOptimalDriverFn isOptimalDriver = nullptr;
		DestroyContextFn destroyContext = nullptr;

		void Reset();
	};

	bool ResolveApi();
	bool EnsureContext();
	/** @brief Records the calling thread as the current SDK caller, logging the first move. */
	void AdoptCallingThread(const char* a_operation) const;
	bool ValidateExecutionResources(
		ID3D11Resource* a_color,
		ID3D11Resource* a_depth,
		ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_responsiveMask,
		ID3D11Resource* a_output,
		uint32_t a_inputWidth,
		uint32_t a_inputHeight) const;
	bool LogResult(const char* a_operation, xess_result_t a_result, bool a_warningIsSuccess = true) const;
	void ResetConfiguration();

	HMODULE module_ = nullptr;
	Api api_{};
	xess_context_handle_t context_ = nullptr;
	winrt::com_ptr<ID3D11Device> device_;
	/** Thread of the most recent SDK call; diagnostic only, adopted on change (see AdoptCallingThread). */
	mutable std::thread::id ownerThread_{};

	xess_2d_t outputResolution_{};
	InputResolutionRange inputResolutionRange_{};
	xess_quality_settings_t quality_ = XESS_QUALITY_SETTING_QUALITY;
	uint32_t initFlags_ = XESS_INIT_FLAG_NONE;
	bool useResponsiveMask_ = false;
	bool useAutoExposure_ = false;
	float velocityScaleX_ = 0.0f;
	float velocityScaleY_ = 0.0f;
	xess_version_t sdkVersion_{};
	xess_version_t intelXeFXVersion_{};
	xess_result_t lastExecuteResult_ = XESS_RESULT_SUCCESS;
	bool executeResultLogged_ = false;
};
