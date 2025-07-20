#pragma once
#include "Menu.h"
#include "OverlayFeature.h"
#include <atomic>
#include <d3d11.h>
#include <magic_enum.hpp>
#include <openvr.h>
#include <vector>
using namespace DirectX::SimpleMath;

// Controller device enum
enum class ControllerDevice
{
	Primary = 0,
	Secondary = 1,
	Both = 2
};

// Button combo structure - using explicit encoding for better JSON compatibility
struct ButtonCombo
{
	uint32_t deviceAndKey;  // device in upper bits, key in lower bits

	ButtonCombo(ControllerDevice device, uint32_t key) :
		deviceAndKey((static_cast<uint32_t>(device) << 16) | (key & 0xFFFF)) {}

	// Helper constructors for common cases
	static ButtonCombo Primary(uint32_t key) { return ButtonCombo(ControllerDevice::Primary, key); }
	static ButtonCombo Secondary(uint32_t key) { return ButtonCombo(ControllerDevice::Secondary, key); }
	static ButtonCombo Both(uint32_t key) { return ButtonCombo(ControllerDevice::Both, key); }

	// Accessors
	ControllerDevice GetDevice() const { return static_cast<ControllerDevice>(deviceAndKey >> 16); }
	uint32_t GetKey() const { return deviceAndKey & 0xFFFF; }

	// Default constructor for JSON
	ButtonCombo() :
		deviceAndKey(0) {}
};

// JSON serialization for ButtonCombo (simple uint32_t)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ButtonCombo, deviceAndKey)

struct VR : OverlayFeature
{
	static VR* GetSingleton()
	{
		static VR singleton;
		return &singleton;
	}

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

	struct Settings
	{
		bool EnableDepthBufferCulling = true;
		float MinOccludeeBoxExtent = 10.0f;

		// VR Menu Overlay Settings
		float VRMenuDistance = 1.5f;
		float VRMenuScale = 1.0f;         // 0.5x to 2.0x
		int VRMenuPositioningMethod = 0;  // 0 = HMD relative, 1 = Fixed world position

		enum class OverlayAttachMode
		{
			HMDOnly = 0,
			ControllerOnly = 1,
			Both = 2
		};
		OverlayAttachMode attachMode = OverlayAttachMode::Both;
		// Use OpenVR's ETrackedControllerRole for hand selection
		vr::ETrackedControllerRole VRMenuControllerHand = vr::ETrackedControllerRole::TrackedControllerRole_LeftHand;

		// HMD overlay offset settings (separate from controller)
		float VRMenuOffsetX = 0.0f;  // Left/Right offset
		float VRMenuOffsetY = 0.0f;  // Up/Down offset
		float VRMenuOffsetZ = 0.0f;  // Forward/Back offset

		// Controller offset settings (separate from HMD)
		float VRMenuControllerOffsetX = 0.0f;  // Left/Right offset
		float VRMenuControllerOffsetY = 0.1f;  // Up/Down offset
		float VRMenuControllerOffsetZ = 0.2f;  // Forward/Back offset

		// Input settings
		bool VRMenuControllerDiagnosticsTestMode = false;  // If true, disables controller input for menu except right thumbstick and triggers

		// VR menu mouse control settings
		float mouseDeadzone = 0.1f;  // Minimum thumbstick deflection to move mouse/scroll
		float mouseSpeed = 10.0f;    // Mouse speed in pixels per frame per full deflection

		// Drag highlight color (RGBA)
		std::array<float, 4> dragHighlightColor = { 1.0f, 1.0f, 0.0f, 0.3f };  // Yellow tint with 30% alpha

		// VR Key Bindings - Using ButtonCombo structure for cleaner device/key separation
		std::vector<ButtonCombo> VRMenuOpenKeys = {
			ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA)),
			ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY))
		};  // A/X and B/Y on primary
		std::vector<ButtonCombo> VRMenuCloseKeys = {
			ButtonCombo::Both(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip))
		};  // Grips on both
		std::vector<ButtonCombo> VROverlayOpenKeys = {
			ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};  // Joystick click on primary
		std::vector<ButtonCombo> VROverlayCloseKeys = {
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};  // Joystick click on secondary

		// VR Combo Settings
		float comboTimeout = 3.0f;  // Timeout in seconds for combo sequences
		bool ShowHowToUseMessage = true;
		bool EnableDragToReposition = true;
	};

	Settings settings;

	virtual void DrawSettings() override;
	virtual void DrawOverlay() override;
	bool IsOverlayVisible() const override { return settings.ShowHowToUseMessage; }

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	// VR Menu Overlay functions
	void UpdateVROverlayPosition();
	void UpdateVROverlayControllerPosition();

private:
	vr::HmdMatrix34_t ComputeOverlayTransformFromHMD();

public:
	// VR input processing (event loop)
	void ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents);
	// Overlay/menu open/close logic based on controller input state
	void UpdateOverlayMenuStateFromInput();
	// VR input processing
	void ProcessVRButtonEvent(const Menu::KeyEvent& event);
	void ProcessVRThumbstickEvent(const Menu::KeyEvent& event);
	// Maps VR controller thumbstick input to ImGui mouse and scroll events for the overlay UI
	void ProcessVRControllerOverlayInput();

public:
	// Overlay handles and D3D11 textures
	vr::VROverlayHandle_t menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	ID3D11Texture2D* menuTexture = nullptr;
	ID3D11RenderTargetView* menuRTV = nullptr;
	ID3D11Texture2D* menuControllerTexture = nullptr;
	ID3D11RenderTargetView* menuControllerRTV = nullptr;

	void EnsureOverlayInitialized();
	void DestroyOverlay();
	void RecreateOverlayTexturesIfNeeded();
	void SubmitOverlayFrame();

	bool* gDepthBufferCulling = nullptr;
	float* gMinOccludeeBoxExtent = nullptr;

	// VR controller event log struct and vector
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
		std::string controllerRole;  // For thumbstick events, keyCode/value are replaced by x/y floats
	};
	std::vector<VRControllerEventLog> vrControllerEventLog;
	RE::InputDeviceState primaryControllerState;
	RE::InputDeviceState secondaryControllerState;

	// Non-persistent fixed world overlay position (session only)
	struct OverlayWorldPosition
	{
		Matrix m = Matrix::Identity;  // 3x4 matrix (rotation + translation)
	} fixedWorldOverlayPosition;

	// Drag state for overlay manipulation
	struct OverlayDragState
	{
		bool dragging = false;
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		bool isPrimary = false;
		bool isSecondary = false;
		// For fixed world drag
		Matrix initialControllerMatrix = Matrix::Identity;
		Matrix initialOverlayMatrix = Matrix::Identity;
		Matrix grabOffset = Matrix::Identity;  // overlay^-1 * controller at grab
		bool intersecting = false;             // True if any controller is currently intersecting the overlay

		// For HMD/controller overlay drag
		enum class DragMode
		{
			None,
			FixedWorld,
			HMD,
			Controller
		} mode = DragMode::None;
		// For HMD overlay drag
		Vector3 initialHMDOffset = Vector3::Zero;
		// For controller overlay drag
		Vector3 initialControllerOffset = Vector3::Zero;

		// For refactored drag logic
		Matrix startControllerMatrix = Matrix::Identity;
	};

	OverlayDragState overlayDragState;

	// VR combo sequence tracking
	struct ComboSequence
	{
		std::vector<uint32_t> sequence;
		double startTime = 0.0;
		size_t currentIndex = 0;
		bool active = false;
	};
	ComboSequence menuOpenCombo;
	ComboSequence menuCloseCombo;

	// Combo recording state
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
	double comboTimeout = 3.0;  // 3 second timeout

	void UpdateOverlayDrag();
	void SetFixedOverlayToCurrentHMD();
	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted);

	// Returns true if the overlay window should be highlighted (dragging in any mode)
	bool ShouldHighlightOverlayWindow() const
	{
		return overlayDragState.dragging;
	}
};