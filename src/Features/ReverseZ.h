#pragma once

struct ReverseZ : Feature
{
public:
	virtual inline std::string GetName() override { return "Reverse Z-Buffer"; }
	virtual inline std::string GetShortName() override { return "ReverseZ"; }
	virtual inline std::string_view GetShaderDefineName() override { return "REVERSE_Z"; }
	virtual std::string_view GetCategory() const override { return "Rendering"; }
	virtual bool IsCore() const override { return true; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Reverse Z-Buffer improves depth precision by inverting the depth buffer to use floating-point values more efficiently.\n"
			"This technique significantly reduces z-fighting and improves depth accuracy, especially at far distances.",
			{ "Improved depth precision",
			  "Reduced z-fighting artifacts",
			  "Better far plane accuracy",
			  "Enhanced depth testing",
			  "Optimized floating-point usage" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct Settings
	{
		bool EnableReverseZ = true;
	};

	Settings settings;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual bool SupportsVR() override { return true; }
	virtual void PostPostLoad() override;
};