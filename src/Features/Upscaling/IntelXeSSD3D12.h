#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <Windows.h>
#include <winrt/base.h>

#include <xess/xess_d3d12.h>

/**
 * @brief Owns the optional cross-vendor XeSS-SR D3D12 backend.
 *
 * This is the fallback path for non-Intel adapters. Rendering remains D3D11;
 * shared textures and a shared fence hand each XeSS dispatch to D3D12.
 * XeSS is not thread safe, so the SDK calls must be serialized. The engine already
 * provides that, but it moves them between the render and main threads, so there is
 * no fixed owning thread; see AdoptCallingThread.
 */
class IntelXeSSD3D12
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\XeSS";
	static constexpr const wchar_t* DllName = L"libxess.dll";

	struct InputResolutionRange
	{
		xess_2d_t optimal{};
		xess_2d_t minimum{};
		xess_2d_t maximum{};
	};

	IntelXeSSD3D12() = default;
	~IntelXeSSD3D12();

	IntelXeSSD3D12(const IntelXeSSD3D12&) = delete;
	IntelXeSSD3D12& operator=(const IntelXeSSD3D12&) = delete;
	IntelXeSSD3D12(IntelXeSSD3D12&&) = delete;
	IntelXeSSD3D12& operator=(IntelXeSSD3D12&&) = delete;

	/** @brief True after an attempt has been made to load the optional XeSS DLL. */
	bool triedLoad = false;
	/** @brief True after XeSS successfully creates a context for the D3D12 device. */
	bool available = false;
	/** @brief True after the context has been initialized for an output configuration. */
	bool initialized = false;
	/** @brief True when the runtime accepts the device but recommends a newer graphics driver. */
	bool oldDriverWarning = false;

	/** @brief Loads libxess.dll and resolves the required API entry points. */
	bool Load();

	/**
	 * @brief Binds the D3D12 device. The context itself is created on first use so that it,
	 *        and every later SDK call, lives on the render thread as the SDK requires.
	 * @return False for a missing DLL or incomplete API; hardware support is discovered on first use.
	 */
	bool Initialize(ID3D12Device* a_device);

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
	 * @brief Records one XeSS-SR dispatch into the caller's D3D12 command list.
	 *
	 * The caller owns recording, submission and synchronisation of the command list, and must
	 * leave every resource in the state XeSS expects. Motion vectors must be undilated,
	 * jitter-free, current-to-previous vectors in normalized viewport units; they are converted
	 * to pixels using the actual input dimensions. Input and output color must not alias.
	 */
	bool Upscale(
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
		float a_exposureScale = 1.0f);
	/** @brief Destroys the XeSS context after the caller has completed pending GPU work. */
	bool DestroyResources();
	/** @brief Destroys the context and unloads the optional XeSS DLL. */
	bool Shutdown();

	[[nodiscard]] bool IsRuntimeAvailable() const { return module_ != nullptr; }
	[[nodiscard]] const InputResolutionRange& GetInputResolutionRange() const { return inputResolutionRange_; }
	[[nodiscard]] const xess_2d_t& GetOutputResolution() const { return outputResolution_; }
	[[nodiscard]] xess_quality_settings_t GetQuality() const { return quality_; }
	[[nodiscard]] bool UsesResponsiveMask() const { return useResponsiveMask_; }
	[[nodiscard]] bool UsesAutoExposure() const { return useAutoExposure_; }

private:
	using GetVersionFn = decltype(&xessGetVersion);
	using GetIntelXeFXVersionFn = decltype(&xessGetIntelXeFXVersion);
	using CreateContextFn = decltype(&xessD3D12CreateContext);
	using BuildPipelinesFn = decltype(&xessD3D12BuildPipelines);
	using InitializeContextFn = decltype(&xessD3D12Init);
	using ExecuteFn = decltype(&xessD3D12Execute);
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
		BuildPipelinesFn buildPipelines = nullptr;
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
		ID3D12Resource* a_color,
		ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors,
		ID3D12Resource* a_responsiveMask,
		ID3D12Resource* a_output,
		uint32_t a_inputWidth,
		uint32_t a_inputHeight) const;
	bool LogResult(const char* a_operation, xess_result_t a_result, bool a_warningIsSuccess = true) const;
	void ResetConfiguration();

	HMODULE module_ = nullptr;
	Api api_{};
	xess_context_handle_t context_ = nullptr;
	winrt::com_ptr<ID3D12Device> device_;
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
	xess_result_t lastExecuteResult_ = XESS_RESULT_SUCCESS;
	bool executeResultLogged_ = false;
};
