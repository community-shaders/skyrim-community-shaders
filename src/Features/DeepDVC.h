#pragma once

#include "../Feature.h"
#include "Upscaling/Streamline.h"

struct DeepDVC : public Feature
{
public:
	virtual inline std::string GetName() override { return "DeepDVC"; }
	virtual inline std::string GetShortName() override { return "DeepDVC"; }
	virtual inline std::string_view GetCategory() const override { return "Display"; }

	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"RTX Dynamic Vibrance uses AI to enhance digital vibrance in real-time.",
			{ "Improves visual clarity",
				"Adjusts color saturation adaptively",
				"Requires NVIDIA RTX GPU" }
		};
	}

	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return false; }
	virtual bool IsInMenu() const override;
	virtual void PostPostLoad() override;

	struct Settings
	{
		uint32_t mode = 1;
		float intensity = 0.3f;
		float saturationBoost = 0.2f;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, mode, intensity, saturationBoost);
	} settings;

	void Evaluate();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	~DeepDVC()
	{
		if (proxyBuffer) {
			proxyBuffer->Release();
			proxyBuffer = nullptr;
		}
	}

private:
	bool IsSupported() const;
	ID3D11Texture2D* proxyBuffer = nullptr;
	bool missingInput = false;
};
