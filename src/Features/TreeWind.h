#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "TreeWind/TreeShapes.h"

/**
 * @brief Wind sway for DynDOLOD tree LOD.
 *
 * Keeps a wind clock that follows the weather and binds it to the vertex shader (b3) for Lighting and
 * Utility draws. The TREE_WIND define adds the matching vertex offset to Lighting.hlsl and Utility.hlsl,
 * so the colour, depth and shadow passes move together. The object LOD vertex shaders cannot tell tree
 * LOD apart at compile time, so each draw gets either the swaying (on) or the still (off) buffer.
 */
struct TreeWind : Feature
{
private:
	static constexpr std::string_view MOD_ID = "195280";

public:
	struct Settings
	{
		bool Enabled = true;
		float Amplitude = 80.0f;
		float BaseSpeed = 0.6f;
		float WindSpeedGain = 1.4f;
		float BendExponent = 1.5f;
		float TreeSpread = 2.5f;
		float Flutter = 0.25f;
		float FlutterShare = 0.6f;
		float FlutterSpeed = 7.0f;
		float MinDistance = 10000.0f;
		float NearScale = 0.6f;
		float NearRampEnd = 40000.0f;
		float GustStrengthCalm = 1.0f;
		float GustRarityCalm = 40.0f;
		float GustStrengthStorm = 4.0f;
		float GustRarityStorm = 4.0f;
		float GustWindCalm = 0.15f;
		float GustWindStorm = 0.6f;
		float GustRainy = 1.0f;
		float GustSnow = 0.85f;
		float GustCloudy = 0.35f;
		bool TreeModSupport = true;
	};

	/** @brief Mirrors TreeWindCB in TreeWind.hlsli. */
	struct alignas(16) WindCB
	{
		float4 Wind;    // phase, previous phase, wind speed 0-1, wind angle (radians)
		float4 Params;  // amplitude, near distance, enabled, unused
		float4 Shape;   // bend exponent, use bend weights, flutter strength, per-tree phase spread
		float4 Detail;  // flutter share, flutter speed, near scale, near ramp end
		float4 Gusts;   // gust strength, gust rarity, unused, unused
	};
	static_assert(sizeof(WindCB) % 16 == 0);

	enum class Preset
	{
		Gentle,
		Default,
		Windy
	};

	Settings settings;

	virtual inline std::string GetName() override { return "Tree Wind"; }
	virtual inline std::string GetDisplayName() override { return T("feature.tree_wind.name", "Tree Wind"); }
	virtual inline std::string GetShortName() override { return "TreeWind"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TREE_WIND"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
	{
		return t == RE::BSShader::Type::Lighting || t == RE::BSShader::Type::Utility || t == RE::BSShader::Type::DistantTree;
	}
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.tree_wind.description", "Lets the distant tree LOD sway in the wind."),
			{ T("feature.tree_wind.key_feature_1", "Sway follows the weather: gentle in clear skies, strong gusts in storms."),
				T("feature.tree_wind.key_feature_2", "Each tree bends from its base and keeps its own timing.") } };
	}

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Stands down when the old Windswept Tree LOD plugin is installed, which sways the same trees. */
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;

	/** @brief Advances the wind clock and uploads the on and off buffers, once per frame. */
	virtual void Reset() override;

	/** @brief Binds the wind buffer, and the bend data for a tree LOD draw, for the current Lighting or Utility draw. Called from State::Draw. */
	void SetShaderResources(ID3D11DeviceContext* a_context);

	void ApplyPreset(Preset a_preset);

	/** @brief Picks out the tree LOD draws between SetupGeometry and RestoreGeometry of the Lighting and Utility shaders. */
	void BeginDraw(RE::BSRenderPass* a_pass);
	void EndDraw();

	struct Hooks;

private:
	/** @brief How stormy a weather is, 0 calm to 1 storm: the larger of its type and its own wind speed. */
	float GetStorminess(const RE::TESWeather* a_weather) const;

	std::unique_ptr<ConstantBuffer> windOnCB;
	std::unique_ptr<ConstantBuffer> windOffCB;

	TreeShapes::BendWeights bendWeights;
	// Bend data of the tree LOD shape being drawn, nullptr for every other draw
	ID3D11ShaderResourceView* drawBendWeights = nullptr;
	bool swayActive = false;  // set once per frame in Reset: sway on and no map menu open

	double phase = 0.0;
	double prevPhase = 0.0;

	// Shown on the settings page
	float statusWind = 0.0f;
	float statusStorminess = 0.0f;
	float statusGust = 0.0f;

	// Bend data built with the other tree-mod setting is freed
	bool bendWeightsTreeModSupport = true;
};
