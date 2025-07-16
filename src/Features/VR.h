#pragma once

class Menu;  // forward declaration

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
		float VRMenuHeight = 0.0f;
		float VRMenuWidth = 1.0f;
		int VRMenuPositioningMethod = 0;  // 0 = HMD relative, 1 = Fixed world position

		// Attach point selection (multi-select)
		bool VRMenuAttachToHMD = true;         // Show on HMD
		bool VRMenuAttachToController = true;  // Show on controller
		int VRMenuControllerHand = 0;          // 0 = left, 1 = right

		// HMD overlay offset settings (separate from controller)
		float VRMenuOffsetX = 0.0f;  // Left/Right offset
		float VRMenuOffsetY = 0.0f;  // Up/Down offset
		float VRMenuOffsetZ = 0.0f;  // Forward/Back offset

		// Controller offset settings (separate from HMD)
		float VRMenuControllerOffsetX = 0.0f;  // Left/Right offset
		float VRMenuControllerOffsetY = 0.1f;  // Up/Down offset
		float VRMenuControllerOffsetZ = 0.2f;  // Forward/Back offset

		// Input settings
		bool VRMenuEnableControllerInput = true;  // Enable controller input interaction
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

	bool* gDepthBufferCulling = nullptr;
	float* gMinOccludeeBoxExtent = nullptr;
};