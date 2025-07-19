#pragma once
#include "Menu.h"
#include <atomic>
#include <d3d11.h>
#include <magic_enum.hpp>
#include <openvr.h>
#include <vector>
using namespace DirectX::SimpleMath;

struct VR : Feature
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
		float mouseDeadzone = 0.2f;  // Minimum thumbstick deflection to move mouse/scroll
		float mouseSpeed = 25.0f;    // Mouse speed in pixels per frame per full deflection
	};

	Settings settings;

	virtual void DrawSettings() override;

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

	void UpdateOverlayDrag();
	void SetFixedOverlayToCurrentHMD();

	// Returns true if the overlay window should be highlighted (dragging in any mode)
	bool ShouldHighlightOverlayWindow() const
	{
		return overlayDragState.dragging;
	}
};