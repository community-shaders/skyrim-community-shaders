#pragma once

struct HairSpecular : Feature
{
private:
	static constexpr std::string_view MOD_ID = "149011";

public:
	static HairSpecular* GetSingleton()
	{
		static HairSpecular singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Hair Specular"; }
	virtual inline std::string GetShortName() override { return "HairSpecular"; }
	virtual inline std::string_view GetShaderDefineName() override { return "CS_HAIR"; }
	virtual std::string_view GetCategory() const override { return "Characters"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides better hair shading with realistic specular highlights and tangent-based light interaction for more lifelike hair appearance.",
			{ "Realistic hair specular highlights",
				"Enhanced hair glossiness and saturation controls",
				"Separate specular and diffuse lighting multipliers",
				"Tangent shift texture support for varied hair highlights" }
		};
	}
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; };

	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }

	virtual void Prepass() override;

	virtual void SetupResources() override;

	struct alignas(16) Settings
	{
		uint Enabled = true;
		float HairGlossiness = 80.0f;
		float SpecularMult = 1.0f;
		float DiffuseMult = 1.0f;
		uint EnableTangentShift = true;
		float PrimaryTangentShift = 0.5f;
		float SecondaryTangentShift = -0.25f;
		float HairSaturation = 1.0f;
		float SpecularIndirectMult = 1.0f;
		float DiffuseIndirectMult = 1.0f;
		float BaseColorMult = 1.0f;
		float Transmission = 0.5f;
	} settings;

	eastl::unique_ptr<Texture2D> texTangentShift = nullptr;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
};