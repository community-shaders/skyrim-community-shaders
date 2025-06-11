#pragma once

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
			{
				"Depth buffer culling optimization for VR performance",
				"Configurable occlusion culling parameters",
				"VR-specific rendering pipeline improvements",
				"Performance optimizations for dual-eye rendering",
				"Enhanced VR compatibility across all shader features"
			}
		};
	}

	struct Settings
	{
		bool EnableDepthBufferCulling = true;
		float MinOccludeeBoxExtent = 10.0f;
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

	bool* gDepthBufferCulling = nullptr;
	float* gMinOccludeeBoxExtent = nullptr;
};
