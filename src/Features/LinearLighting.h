#pragma once

struct LinearLighting : Feature
{
    static LinearLighting* GetSingleton()
    {
        static LinearLighting singleton;
        return &singleton;
    }

    virtual inline std::string GetName() override { return "Linear Lighting"; }
    virtual inline std::string GetShortName() override { return "LinearLighting"; }

    virtual bool SupportsVR() override { return true; };
    virtual bool IsCore() const override { return true; };

    struct Settings
    {
        uint enableLinearLighting = true;
        uint enableGammaCorrection = true;
        uint pad[2];
    } settings;

    virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;
};
