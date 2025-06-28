
/*
* This file defines a new feature template for Community Shader.
* Copy the .h and .cpp files to src/Features and rename them to your feature's name.
* Replace all NewFeature occurances in both files as well, and change the metadata accordingly.
* Don't forget to add the feature to the feature list in src/Feature.cpp,
* and copy and rename the "New Feature" folder and contents to features/ so it gets registered.
*
* The naming and coding style are adapted to my personal practice,
* but we don't really have a strict, solidified guideline on that.
* So take your liberties within reason.
*
* Cheers,
* ProfJack
* 2025-06-28
*/

#pragma once

struct NewFeature : public Feature
{
	////////////////////////////////////////////////// Boilerplate
	static NewFeature* GetSingleton()
	{
		static NewFeature singleton;
		return &singleton;
	}

	// Metadata
	virtual inline std::string GetName() override { return "New Feature"; }
	virtual inline std::string GetShortName() override { return "NewFeature"; }
	virtual inline std::string_view GetCategory() const override { return "Lighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"This is a terse description.",
			{
				"This is a subfeature.",
				"This is another subfeature.",
				"Cheese.",
			}
		};
	}

	// Functionality
	virtual bool inline SupportsVR() override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "SHADER_MACRO"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	// Resources
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileShaders();

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		float3 ColorA = { 0.3f, 0.5f, 0.7f };
		std::array<uint, 2> IdA = { 1, 2 };  // std::array is because we haven't defined XMUINT2 serialization yet
		float2 UvA = { 0.0f, 0.0f };
	} settings;

	struct CbData
	{
		float3 ColorA;
		float _pad0;  // Padding to align to 16 bytes
		DirectX::XMUINT2 IdA;
		float2 UvA;
	};
	static_assert(sizeof(CbData) % 16 == 0,
		"CbData must be aligned to 16 bytes. "
		"Check out maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer/ if you're unsure.");

	eastl::unique_ptr<ConstantBuffer> cheeseCb = nullptr;  // Omit this if you want to put your CB in src/FeatureBuffer.cpp
	eastl::unique_ptr<Texture2D> cheeseTex = nullptr;
	winrt::com_ptr<ID3D11SamplerState> cheeseSampler = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cheeseCs = nullptr;
};