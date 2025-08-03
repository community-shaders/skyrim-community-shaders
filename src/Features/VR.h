#pragma once
#include "Menu.h"
#include "OverlayFeature.h"
#include <algorithm>
#include <atomic>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <magic_enum.hpp>
#include <openvr.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>
using namespace DirectX::SimpleMath;

/**
 * @brief Identifies which VR controller(s) to target for input actions
 *
 * This enum is used throughout the VR system to specify which controller
 * should handle specific input events or UI interactions.
 */
enum class ControllerDevice
{
	Primary = 0,    ///< The dominant hand controller (right for right-handed, left for left-handed)
	Secondary = 1,  ///< The non-dominant hand controller
	Both = 2        ///< Both controllers simultaneously
};

/**
 * @brief Converts a ControllerDevice enum value to a human-readable string
 * @param device The controller device to convert
 * @return String representation of the device ("Primary", "Secondary", "Both", or "Unknown")
 */
constexpr const char* ToString(ControllerDevice device)
{
	switch (device) {
	case ControllerDevice::Primary:
		return "Primary";
	case ControllerDevice::Secondary:
		return "Secondary";
	case ControllerDevice::Both:
		return "Both";
	default:
		return "Unknown";
	}
}

/**
 * @brief Validates if a ControllerDevice enum value is within valid range
 * @param device The controller device to validate
 * @return true if the device value is valid, false otherwise
 */
constexpr bool IsValidDevice(ControllerDevice device)
{
	return device >= ControllerDevice::Primary && device <= ControllerDevice::Both;
}

/**
 * @brief Represents a combination of controller device and button/key for VR input mapping
 *
 * This structure efficiently encodes both the target controller and the specific button
 * into a single 32-bit value for performance and JSON serialization compatibility.
 * The upper 16 bits store the device type, lower 16 bits store the key code.
 *
 * @example
 * ```cpp
 * // Create a combo for the A button on the secondary controller
 * auto combo = ButtonCombo::Secondary(RE::BSOpenVRControllerDevice::Keys::kXA);
 *
 * // Check which device and key
 * ControllerDevice device = combo.GetDevice();
 * uint32_t key = combo.GetKey();
 * ```
 */
struct ButtonCombo
{
private:
	uint32_t deviceAndKey;  // device in upper bits, key in lower bits

public:
	/**
	 * @brief Constructs a ButtonCombo with the specified device and key
	 * @param device The target controller device
	 * @param key The button/key code (must fit in 16 bits, values > 0xFFFF will be truncated)
	 */
	ButtonCombo(ControllerDevice device, uint32_t key) :
		deviceAndKey((static_cast<uint32_t>(device) << 16) | (key & 0xFFFF))
	{
		// Validate that the device is within valid range
		if (!IsValidDevice(device)) {
			logger::warn("ButtonCombo: Invalid device value {} ({}), using as-is",
				static_cast<uint32_t>(device), magic_enum::enum_name(device));
		}

		// Validate that the key fits within 16 bits to prevent silent data loss
		if (key > 0xFFFF) {
			logger::warn("ButtonCombo: Key value 0x{:X} exceeds 16-bit limit (0xFFFF), truncating to 0x{:X}",
				key, key & 0xFFFF);
		}
	}

	/**
	 * @brief Creates a ButtonCombo for the primary controller
	 * @param key The button/key code
	 * @return ButtonCombo targeting the primary controller
	 */
	static ButtonCombo Primary(uint32_t key) { return ButtonCombo(ControllerDevice::Primary, key); }

	/**
	 * @brief Creates a ButtonCombo for the secondary controller
	 * @param key The button/key code
	 * @return ButtonCombo targeting the secondary controller
	 */
	static ButtonCombo Secondary(uint32_t key) { return ButtonCombo(ControllerDevice::Secondary, key); }

	/**
	 * @brief Creates a ButtonCombo for both controllers
	 * @param key The button/key code
	 * @return ButtonCombo targeting both controllers
	 */
	static ButtonCombo Both(uint32_t key) { return ButtonCombo(ControllerDevice::Both, key); }

	/**
	 * @brief Gets the controller device from this combo
	 * @return The target controller device
	 */
	ControllerDevice GetDevice() const { return static_cast<ControllerDevice>(deviceAndKey >> 16); }
	/**
	 * @brief Gets the button/key code from this combo
	 * @return The button/key code (16-bit value)
	 */
	uint32_t GetKey() const { return deviceAndKey & 0xFFFF; }

	/**
	 * @brief Validates if this ButtonCombo has valid device and key values
	 * @return true if both device and key are valid, false otherwise
	 */
	bool IsValid() const
	{
		return IsValidDevice(GetDevice()) && GetKey() != 0;
	}

	/**
	 * @brief Equality comparison operator for container usage
	 * @param other The ButtonCombo to compare with
	 * @return true if both combos represent the same device and key
	 */
	bool operator==(const ButtonCombo& other) const
	{
		return deviceAndKey == other.deviceAndKey;
	}

	/**
	 * @brief Less-than comparison operator for ordered container usage
	 * @param other The ButtonCombo to compare with
	 * @return true if this combo sorts before the other
	 */
	bool operator<(const ButtonCombo& other) const
	{
		return deviceAndKey < other.deviceAndKey;
	}

	/**
	 * @brief Creates a human-readable string representation for debugging
	 * @return String in format "Device:Key" (e.g., "Primary:123")
	 */
	std::string ToString() const
	{
		return std::string(::ToString(GetDevice())) + ":" + std::to_string(GetKey());
	}

	/**
	 * @brief Default constructor for JSON serialization compatibility
	 */
	ButtonCombo() :
		deviceAndKey(0) {}

	/**
	 * @brief JSON serialization support - converts ButtonCombo to JSON
	 * @param j Output JSON object
	 * @param combo ButtonCombo to serialize
	 */
	friend void to_json(nlohmann::json& j, const ButtonCombo& combo)
	{
		j = combo.deviceAndKey;
	}

	/**
	 * @brief JSON deserialization support - creates ButtonCombo from JSON
	 * @param j Input JSON object
	 * @param combo ButtonCombo to populate
	 */
	friend void from_json(const nlohmann::json& j, ButtonCombo& combo)
	{
		combo.deviceAndKey = j.get<uint32_t>();
	}
};

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
		static constexpr float kDefaultMenuScale = 1.0f;      ///< Default overlay scale factor
		static constexpr float kMinMenuScale = 0.5f;          ///< Minimum allowed overlay scale
		static constexpr float kMaxMenuScale = 2.0f;          ///< Maximum allowed overlay scale
		static constexpr float kDefaultComboTimeout = 3.0f;   ///< Default timeout for button combos (seconds)
		static constexpr float kDefaultMouseDeadzone = 0.1f;  ///< Default thumbstick deadzone for mouse input
		static constexpr float kDefaultMouseSpeed = 10.0f;    ///< Default mouse speed multiplier
		static constexpr int kDefaultAutoHideSeconds = 30;    ///< Default auto-hide timeout for overlay messages
		static constexpr int kMaxAutoHideSeconds = 300;       ///< Maximum auto-hide timeout (5 minutes)

		// Default HMD overlay offset values (in meters, relative to HMD)
		static constexpr float kDefaultHMDOffsetX = 0.26f;   ///< Default horizontal offset from HMD
		static constexpr float kDefaultHMDOffsetY = -0.04f;  ///< Default vertical offset from HMD
		static constexpr float kDefaultHMDOffsetZ = -0.41f;  ///< Default depth offset from HMD

		// Default controller overlay offset values (in meters, relative to controller)
		static constexpr float kDefaultControllerOffsetX = 0.22f;  ///< Default horizontal offset from controller
		static constexpr float kDefaultControllerOffsetY = 0.15f;  ///< Default vertical offset from controller
		static constexpr float kDefaultControllerOffsetZ = 0.20f;  ///< Default depth offset from controller
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
				"Configurable occlusion culling parameters",
				"VR-specific rendering pipeline improvements",
				"Performance optimizations for dual-eye rendering",
				"Enhanced VR compatibility across all shader features" }
		};
	}

	virtual void SetupResources() override;
	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual void DrawSettings() override;

	virtual std::string_view GetCategory() const override { return "Debug"; }

	//=============================================================================
	// OVERLAY FEATURE OVERRIDES
	//=============================================================================

	virtual void DrawOverlay() override;
	virtual bool IsOverlayVisible() const override { return openVRInfo.isCompatible && settings.kAutoHideSeconds > 0 && !globals::menu->IsEnabled; }

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
		bool EnableDepthBufferCulling = true;  ///< Enable depth buffer culling for VR performance
		float MinOccludeeBoxExtent = 10.0f;    ///< Minimum bounding box size for occlusion culling

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
			Both = 2             ///< Overlay can be attached to both HMD and controller
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
		bool EnableDragToReposition = true;                      ///< Allow drag-and-drop overlay repositioning

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
			return attachMode >= OverlayAttachMode::HMDOnly && attachMode <= OverlayAttachMode::Both;
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
		}
	};

	Settings settings;  ///< Current VR configuration settings

	//=============================================================================
	// VR-SPECIFIC PUBLIC API
	//=============================================================================

	void UpdateVROverlayPosition();
	void UpdateVROverlayControllerPosition();

	void ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents);
	void UpdateOverlayMenuStateFromInput();
	void ProcessVRButtonEvent(const Menu::KeyEvent& event);
	void UpdateControllerState(const Menu::KeyEvent& event);
	void ProcessControllerInputForImGui();

	void EnsureOverlayInitialized();
	void DestroyOverlay();
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
		bool isCompatible = true;
		std::string dllPath;
		std::string version;
		uint64_t fileSize = 0;
		std::string modificationTime;
	} openVRInfo;

	RE::NiPoint3 savedPlayerWorldPos = RE::NiPoint3();  // Used for auto-reset distance check

public:
	//=============================================================================
	// PRIVATE IMPLEMENTATION
	//=============================================================================

	void DetectOpenVRInfo();
	bool IsOpenVRCompatible() const;
};
