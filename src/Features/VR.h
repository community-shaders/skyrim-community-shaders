#pragma once
#include "Menu.h"
#include "OverlayFeature.h"
#include "Utils/Input.h"
#include "VR/OpenVRDetection.h"  // In Features/VR/
#include "VRStereoOptimizations.h"
#include <algorithm>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <magic_enum/magic_enum.hpp>
#include <openvr.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

using namespace DirectX::SimpleMath;

// Backwards compatibility aliases
using ControllerDevice = InputDeviceType;
using ButtonCombo = InputCombo;

/**
 * @brief Main VR feature class providing VR-specific optimizations and overlay UI system
 *
 * This class extends OverlayFeature to provide comprehensive VR support including:
 * - Performance optimizations (depth buffer culling, occlusion culling)
 * - VR overlay system for in-game UI interaction
 * - Controller input processing and button combo mapping
 * - Overlay positioning and manipulation (HMD-relative, controller-relative, fixed world)
 * - Drag-and-drop overlay repositioning
 *
 * The VR class follows the singleton pattern and integrates with the OpenVR API
 * to provide seamless VR experience within the Community Shaders framework.
 *
 * @example
 * ```cpp
 * // Get the VR singleton instance
 * VR* vr = VR::GetSingleton();
 *
 * // Check if VR is supported
 * if (vr->SupportsVR()) {
 *     // Configure VR settings
 *     vr->settings.EnableDepthBufferCulling = true;
 *     vr->settings.VRMenuScale = 1.2f;
 * }
 * ```
 */
struct VR : OverlayFeature
{
public:
	//=============================================================================
	// NESTED TYPES AND CONSTANTS
	//=============================================================================

	/**
	 * @brief Configuration constants for VR feature defaults and limits
	 *
	 * These constants define the default values and valid ranges for various
	 * VR settings to ensure consistent behavior and prevent invalid configurations.
	 */
	struct Config
	{
		// Overlay texture dimensions
		static constexpr int kOverlayWidth = 1920;                                                                       ///< Overlay texture width in pixels
		static constexpr int kOverlayHeight = 1080;                                                                      ///< Overlay texture height in pixels
		static constexpr float kOverlayAspect = static_cast<float>(kOverlayHeight) / static_cast<float>(kOverlayWidth);  ///< Aspect ratio (height/width)

		static inline Matrix CreateOverlayScaleMatrix(float scale)
		{
			return Matrix::CreateScale(scale, scale * kOverlayAspect, scale);
		}

		static constexpr float kDefaultMenuScale = 1.0f;      ///< Default overlay scale factor
		static constexpr float kMinMenuScale = 0.1f;          ///< Minimum allowed overlay scale
		static constexpr float kMaxMenuScale = 5.0f;          ///< Maximum allowed overlay scale
		static constexpr float kDefaultComboTimeout = 3.0f;   ///< Default timeout for button combos (seconds)
		static constexpr float kDefaultMouseDeadzone = 0.1f;  ///< Default thumbstick deadzone for mouse input
		static constexpr float kDefaultMouseSpeed = 10.0f;    ///< Default mouse speed multiplier
		static constexpr int kDefaultAutoHideSeconds = 30;    ///< Default auto-hide timeout for overlay messages
		static constexpr int kMaxAutoHideSeconds = 300;       ///< Maximum auto-hide timeout (5 minutes)

		// Default HMD overlay offset values (in meters, relative to HMD)
		static constexpr float kDefaultHMDOffsetX = 0.195f;   ///< Default horizontal offset from HMD
		static constexpr float kDefaultHMDOffsetY = -0.375f;  ///< Default vertical offset from HMD
		static constexpr float kDefaultHMDOffsetZ = -1.355f;  ///< Default depth offset from HMD

		// Default controller overlay offset values (in meters, relative to controller)
		static constexpr float kDefaultControllerOffsetX = 0.295f;  ///< Default horizontal offset from controller
		static constexpr float kDefaultControllerOffsetY = 0.211f;  ///< Default vertical offset from controller
		static constexpr float kDefaultControllerOffsetZ = 0.063f;  ///< Default depth offset from controller
	};

	//=============================================================================
	// FEATURE BASE CLASS OVERRIDES
	//=============================================================================

	virtual inline std::string GetName() override { return "VR"; }
	virtual inline std::string GetShortName() override { return "VR"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides VR-specific optimizations and enhancements for Community Shaders, improving performance and visual quality in virtual reality environments.",
			{ "Depth buffer culling optimization for VR performance",
				"In-scene overlay menu with HMD/Controller/Fixed World attach modes",
				"VR controller input with customizable button mappings",
				"Grip-to-drag overlay positioning with depth control",
				"Configurable occlusion culling parameters",
				"Enhanced VR compatibility with SteamVR and OpenComposite" }
		};
	}

	virtual inline std::string_view GetShaderDefineName() override { return "VR_STEREO_OPT"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return stereoOpt.loaded && t == RE::BSShader::Type::Utility; }
	virtual void Reset() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;
	virtual void EarlyPrepass() override;

	void UpdateDepthBufferCulling();

	// Stereo bilateral blend pass - called from Deferred::DeferredPasses after composite
	void DrawStereoBlend();
	static bool AnyScreenSpaceEffectLoaded();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual void DrawSettings() override;

	virtual std::string_view GetCategory() const override { return FeatureCategories::kUtility; }

	//=============================================================================
	// OVERLAY FEATURE OVERRIDES
	//=============================================================================

	virtual void DrawOverlay() override;
	virtual bool IsOverlayVisible() const override { return IsOpenVRCompatible() && settings.kAutoHideSeconds > 0 && globals::menu && !globals::menu->IsEnabled; }

	//=============================================================================
	// SETTINGS STRUCTURE
	//=============================================================================

	/**
	 * @brief Configuration settings for the VR feature
	 *
	 * This structure contains all user-configurable settings for VR functionality,
	 * including performance optimizations, overlay positioning, input mapping, and
	 * visual customization options. Settings are automatically validated and clamped
	 * to valid ranges when loaded or modified.
	 */
	struct Settings
	{
		// Performance optimization settings
		bool EnableDepthBufferCullingExterior = true;  ///< Enable depth buffer culling for VR performance
		bool EnableDepthBufferCullingInterior = true;
		float MinOccludeeBoxExtent = 10.0f;  ///< Minimum bounding box size for occlusion culling

		// Stereo consistency blend pass (post-composite safety net)
		bool EnableStereoBlend = true;            ///< Enable depth-aware bilateral blend between eyes
		float StereoBlendDepthSigma = 0.01f;      ///< Depth sensitivity for bilateral weight (lower = stricter)
		float StereoBlendMaxFactor = 0.1f;        ///< Maximum blend factor; keep low to preserve stereo parallax
		float StereoBlendColorThreshold = 0.02f;  ///< Minimum color difference to trigger blending (luminance)
		int StereoBlendDebugMode = 0;             ///< 0=off, 1=back-check, 2=blend weight, 3=edge detection, 4=overwrite, 5=overwrite Eye1

		// VR Menu Overlay positioning settings
		float VRMenuScale = Config::kDefaultMenuScale;  ///< Scale factor for overlay UI (0.5-2.0)
		int VRMenuPositioningMethod = 1;                ///< 0 = HMD relative, 1 = Fixed world position

		/**
		 * @brief Defines how overlays are attached and positioned in VR space
		 */
		enum class OverlayAttachMode
		{
			HMDOnly = 0,         ///< Overlay attached to HMD only
			ControllerOnly = 1,  ///< Overlay attached to controller only
			Both = 2,            ///< Overlay can be attached to both HMD and controller
			None = 3             ///< Overlay display disabled
		};
		OverlayAttachMode attachMode = OverlayAttachMode::HMDOnly;              ///< Current overlay attachment mode
		ControllerDevice VRMenuAttachController = ControllerDevice::Secondary;  ///< Which controller to attach overlay to

		// HMD overlay offset settings (in meters)
		float VRMenuOffsetX = Config::kDefaultHMDOffsetX;  ///< Horizontal offset from HMD
		float VRMenuOffsetY = Config::kDefaultHMDOffsetY;  ///< Vertical offset from HMD
		float VRMenuOffsetZ = Config::kDefaultHMDOffsetZ;  ///< Depth offset from HMD

		// Controller overlay offset settings (in meters)
		float VRMenuControllerOffsetX = Config::kDefaultControllerOffsetX;  ///< Horizontal offset from controller
		float VRMenuControllerOffsetY = Config::kDefaultControllerOffsetY;  ///< Vertical offset from controller
		float VRMenuControllerOffsetZ = Config::kDefaultControllerOffsetZ;  ///< Depth offset from controller

		// Input and interaction settings
		bool VRMenuControllerDiagnosticsTestMode = false;     ///< Enable controller diagnostics mode
		float mouseDeadzone = Config::kDefaultMouseDeadzone;  ///< Thumbstick deadzone for mouse input (0.0-1.0)
		float mouseSpeed = Config::kDefaultMouseSpeed;        ///< Mouse speed multiplier (0.1-50.0)

		// Wand pointing settings
		bool EnableWandPointing = true;  ///< Enable controller wand/ray-cast pointing (modern VR input)

		// Visual customization
		std::array<float, 4> dragHighlightColor = { 1.0f, 1.0f, 0.0f, 0.3f };  ///< RGBA color for drag highlight

		// Key binding configurations
		std::vector<ButtonCombo> VRMenuOpenKeys = { ///< Button combos to open VR menu
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA)),
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY))
		};
		std::vector<ButtonCombo> VRMenuCloseKeys = { ///< Button combos to close VR menu
			ButtonCombo::Both(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip))
		};
		std::vector<ButtonCombo> VROverlayOpenKeys = { ///< Button combos to open VR overlay
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};
		std::vector<ButtonCombo> VROverlayCloseKeys = { ///< Button combos to close VR overlay
			ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};

		// General interaction settings
		float comboTimeout = Config::kDefaultComboTimeout;       ///< Timeout for button combo sequences (1.0-10.0 seconds)
		int kAutoHideSeconds = Config::kDefaultAutoHideSeconds;  ///< Auto-hide timeout for overlay messages (>0 shows overlay, <=0 hides it)
		bool EnableDragToReposition = false;                     ///< Allow drag-and-drop overlay repositioning

		float VRMenuAutoResetDistance = 1000.0f;  // Default: 1000 units ≈ 14.3 meters

		/**
		 * @brief Validates if the current menu scale is within acceptable range
		 * @return true if scale is between kMinMenuScale and kMaxMenuScale
		 */
		bool IsMenuScaleValid() const
		{
			return VRMenuScale >= Config::kMinMenuScale && VRMenuScale <= Config::kMaxMenuScale;
		}

		/**
		 * @brief Validates if the current attach mode is valid
		 * @return true if attach mode is within valid enum range
		 */
		bool IsAttachModeValid() const
		{
			return attachMode >= OverlayAttachMode::HMDOnly && attachMode <= OverlayAttachMode::None;
		}

		/**
		 * @brief Clamps all settings to their valid ranges
		 *
		 * This method ensures all numeric settings are within acceptable bounds,
		 * automatically correcting any out-of-range values that might have been
		 * loaded from configuration files or set programmatically.
		 */
		void ClampToValidRanges()
		{
			VRMenuScale = std::clamp(VRMenuScale, Config::kMinMenuScale, Config::kMaxMenuScale);
			mouseDeadzone = std::clamp(mouseDeadzone, 0.0f, 1.0f);
			mouseSpeed = std::clamp(mouseSpeed, 0.1f, 50.0f);
			comboTimeout = std::clamp(comboTimeout, 1.0f, 10.0f);
			kAutoHideSeconds = std::clamp(kAutoHideSeconds, 0, Config::kMaxAutoHideSeconds);
			StereoBlendDepthSigma = std::clamp(StereoBlendDepthSigma, 0.001f, 0.1f);
			StereoBlendMaxFactor = std::clamp(StereoBlendMaxFactor, 0.0f, 0.5f);
			StereoBlendColorThreshold = std::clamp(StereoBlendColorThreshold, 0.0f, 0.2f);
			StereoBlendDebugMode = std::clamp(StereoBlendDebugMode, 0, 5);
		}
	};

	Settings settings;  ///< Current VR configuration settings

	//=============================================================================
	// VR-SPECIFIC PUBLIC API
	//=============================================================================

	void ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents);

	// Wand pointing methods
	enum class OverlayType
	{
		HMD,
		Controller
	};
	bool ComputeWandIntersection(vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV);
	bool ComputeWandIntersectionForOverlayType(OverlayType type, vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV);
	void UpdateCursorFromWandPointing();
	void UpdateOverlayMenuStateFromInput();
	void ProcessVRButtonEvent(const Menu::KeyEvent& event);
	void UpdateControllerState(const Menu::KeyEvent& event);
	void ProcessThumbstickScroll(RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone, ImGuiIO& io);
	void ProcessControllerInputForImGui();

	void RecreateOverlayTexturesIfNeeded();
	void SubmitOverlayFrame();

	/**
	 * @brief Context for rendering VR overlays with render target management
	 */
	struct OverlayRenderContext
	{
		vr::IVROverlay* gameOverlay;
		vr::IVROverlay* cleanOverlay;
		RE::BSOpenVR* openvr;
		ID3D11RenderTargetView* oldRTV = nullptr;
		float clearColor[4] = { 0, 0, 0, 0 };

		bool IsValid() const
		{
			return gameOverlay && cleanOverlay && openvr && openvr->vrSystem;
		}

		void SaveRenderTarget()
		{
			globals::d3d::context->OMGetRenderTargets(1, &oldRTV, nullptr);
		}

		void RestoreRenderTarget()
		{
			globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);
			if (oldRTV) {
				oldRTV->Release();
				oldRTV = nullptr;
			}
		}

		void RenderToTexture(ID3D11RenderTargetView* targetRTV)
		{
			globals::d3d::context->OMSetRenderTargets(1, &targetRTV, nullptr);
			globals::d3d::context->ClearRenderTargetView(targetRTV, clearColor);
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}
	};

	void SubmitHMDOverlay(OverlayRenderContext& context);
	void SubmitControllerOverlay(OverlayRenderContext& context);
	void HideAllOverlays(vr::IVROverlay* gameOverlay);

	void UpdateOverlayDrag();
	bool CanPerformDrag();
	void UpdateActiveDrag();
	void TryStartNewDrag();
	void SetFixedOverlayToCurrentHMD();
	void UpdateFixedWorldPositioning();
	bool ShouldHighlightOverlayWindow() const { return overlayDragState.dragging; }

	//=============================================================================
	// PUBLIC MEMBER VARIABLES
	//=============================================================================

	// OpenVR overlay handles and DirectX 11 rendering resources
	vr::VROverlayHandle_t menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	winrt::com_ptr<ID3D11Texture2D> menuTexture;
	winrt::com_ptr<ID3D11RenderTargetView> menuRTV;
	winrt::com_ptr<ID3D11Texture2D> menuControllerTexture;
	winrt::com_ptr<ID3D11RenderTargetView> menuControllerRTV;

	// Stereo blend compute shader resources
	winrt::com_ptr<ID3D11ComputeShader> stereoBlendCS;
	winrt::com_ptr<ID3D11ComputeShader> stereoBlendDebugBackCheckCS;
	winrt::com_ptr<ID3D11ComputeShader> stereoBlendDebugBlendWeightCS;
	winrt::com_ptr<ID3D11ComputeShader> stereoBlendDebugEdgeDetectionCS;
	winrt::com_ptr<ID3D11ComputeShader> stereoBlendOverwriteCS;
	eastl::unique_ptr<Texture2D> stereoBlendCopyTex;
	eastl::unique_ptr<ConstantBuffer> stereoBlendCB;
	winrt::com_ptr<ID3D11SamplerState> stereoBlendLinearSampler;

	VRStereoOptimizations stereoOpt;

	struct alignas(16) StereoBlendCB
	{
		float FrameDim[2];
		float RcpFrameDim[2];
		float DepthSigma;
		float MaxBlendFactor;
		float ColorDiffThreshold;
		float DebugEdgeTint;
		uint32_t DebugMode;
		float FullBlendDistance;
		float POMDepthScale;
		float _pad;
	};

	// Engine hook integration points
	bool* gDepthBufferCulling = nullptr;
	float* gMinOccludeeBoxExtent = nullptr;

	// VR Controller state and logging
	struct VRControllerEventLog
	{
		int device;
		int keyCode;
		int value;
		bool pressed;
		double heldTime;
		std::string heldSource;
		float thumbstickX = 0.0f;
		float thumbstickY = 0.0f;
		std::string controllerRole;
	};

	std::vector<VRControllerEventLog> vrControllerEventLog;
	RE::VRControllerState primaryControllerState;
	RE::VRControllerState secondaryControllerState;
	bool lastKnownLeftHandedMode = false;

	struct OverlayWorldPosition
	{
		Matrix m = Matrix::Identity;
		bool initialized = false;
	} fixedWorldOverlayPosition;

	struct OverlayDragState
	{
		bool dragging = false;
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		bool isPrimary = false;
		bool isSecondary = false;
		Matrix initialControllerMatrix = Matrix::Identity;
		Matrix initialOverlayMatrix = Matrix::Identity;
		Matrix grabOffset = Matrix::Identity;
		bool intersecting = false;

		enum class DragMode
		{
			None,
			FixedWorld,
			HMD,
			Controller
		} mode = DragMode::None;

		Vector3 initialHMDOffset = Vector3::Zero;
		Vector3 initialControllerOffset = Vector3::Zero;
		float initialHMDScale = 1.0f;
		Matrix startControllerMatrix = Matrix::Identity;
	} overlayDragState;

	struct ComboSequence
	{
		std::vector<uint32_t> sequence;
		double startTime = 0.0;
		size_t currentIndex = 0;
		bool active = false;
	};
	ComboSequence menuOpenCombo;
	ComboSequence menuCloseCombo;

	enum class ComboType
	{
		None,
		MenuOpen,
		MenuClose,
		OverlayOpen,
		OverlayClose
	};

	bool isCapturingCombo = false;
	ComboType currentComboType = ComboType::None;
	const char* currentComboName = nullptr;
	std::vector<ButtonCombo> recordedCombo;
	double comboStartTime = 0.0;
	double comboTimeout = 3.0;

	// Button controller recording state for UI settings
	std::unordered_map<uint32_t, ControllerDevice> recordingButtonControllers;

	// OpenVR version and compatibility information
	struct OpenVRInfo
	{
		bool isAvailable = false;
		bool isCompatible = false;
		std::string dllPath;
		std::string version;
		uint64_t fileSize = 0;
		std::string modificationTime;

		// Interface probing results
		bool hasOverlayInterface = false;
		bool hasSystemInterface = false;
		bool hasCompositorInterface = false;

		// Detection metadata
		VRDetection::RuntimeType runtimeType = VRDetection::RuntimeType::Unknown;
		bool probingSucceeded = false;
	} openVRInfo;

	RE::NiPoint3 savedPlayerWorldPos = RE::NiPoint3();  // Used for auto-reset distance check

	// Wand pointing state
	struct WandIntersectionState
	{
		bool isIntersecting = false;
		ImVec2 uvCoordinates = ImVec2(0.0f, 0.0f);
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		Vector3 rayOrigin = Vector3::Zero;
		Vector3 rayDirection = Vector3::Zero;
	} wandState;

	// In-Scene Overlay Rendering Resources (Fallback for incompatible runtimes)
	struct InSceneResources
	{
		winrt::com_ptr<ID3D11VertexShader> vs;
		winrt::com_ptr<ID3D11PixelShader> ps;
		winrt::com_ptr<ID3D11Buffer> vb;
		winrt::com_ptr<ID3D11Buffer> ib;
		winrt::com_ptr<ID3D11Buffer> cb;
		winrt::com_ptr<ID3D11InputLayout> inputLayout;
		winrt::com_ptr<ID3D11BlendState> blendState;
		winrt::com_ptr<ID3D11DepthStencilState> depthState;
		winrt::com_ptr<ID3D11SamplerState> sampler;
		winrt::com_ptr<ID3D11RasterizerState> rasterizerState;

		// Cached SRV to avoid creating every frame
		winrt::com_ptr<ID3D11ShaderResourceView> menuSRV;
		ID3D11Texture2D* cachedMenuTexture = nullptr;

		// Cached RTVs per eye to avoid creating every frame
		struct CachedRTV
		{
			winrt::com_ptr<ID3D11RenderTargetView> rtv;
			ID3D11Texture2D* texture = nullptr;
		};
		CachedRTV cachedEyeRTVs[2];

		bool initialized = false;
	} inSceneResources;

	struct InSceneCB
	{
		Matrix wvp;
	};

	void InitInSceneResources();
	void RenderInSceneOverlay(vr::EVREye eye, ID3D11Texture2D* targetTexture, const vr::VRTextureBounds_t* bounds);
	void InstallSubmitHook();
	void DetectOpenVRInfo();
	bool IsOpenVRCompatible() const;

private:
	//=============================================================================
	// PRIVATE HELPERS
	//=============================================================================

	bool GetGripPressed(bool isLeft, bool isRight) const;
	void ResetComboRecording();
	void ApplyRecordedCombo();
};
