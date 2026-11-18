#pragma once

#include "Ndf.h"

struct CloudStormSettings
{
	std::string name;
	std::string mode = "Supercell";
	bool enabled = true;
	std::string asset;
	std::string coreAsset;
	std::string replacementReference;
	std::vector<std::string> replacementNodes;
	std::vector<std::string> replacementReferences;
	std::vector<std::string> replacementTextures;
	std::string worldspace;
	float2 center = {};
	float radius = 1000.f;
	float backHalfSize = 2000.f;
	float backRotation = 0.f;
	bool mirrorBack = false;
	float coreHeight = 0.3f;
	float altitude = 256.f;
	float height = 1792.f;
	float strength = 1.f;
	float2 topOffset = {};
	float windInfluence = 0.f;
	float rotation = 1.f;
	float updraft = 7.f;
	float density = 0.75f;
	float albedo = 0.999f;
	float3 emission = {};
	float emissionHeight = 0.2f;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudStormSettings, name, mode, enabled, asset, coreAsset, replacementReference, replacementNodes, replacementReferences, replacementTextures, worldspace, center, radius,
	backHalfSize, backRotation, mirrorBack, coreHeight, altitude, height, strength,
	topOffset, windInfluence, rotation, updraft, density, albedo, emission, emissionHeight)

struct CloudStormParcel
{
	float4 sphere = {};
	float4 rotation = { 0.f, 0.f, 0.f, 1.f };
	float4 noiseOffset = {};
	float4 previousSphere = {};
	float4 previousRotation = { 0.f, 0.f, 0.f, 1.f };
	float4 previousNoiseOffset = {};
	float4 flow = {};  // tangent velocity (m/s), relative concentration
};

struct CloudStormData
{
	float4 centerRadiusStrength = {};  // world XY / vortex radius (m), strength
	float4 altitudeMotion = {};        // height zero / span (m), rotation speed, updraft (m/s)
	float4 driftDensity = {};          // top offset XY (m), extinction (m^-1), single scattering albedo
	float4 emission = {};              // linear source RGB, vertical falloff fraction
	float4 animation = {};             // current / previous time (s), NDF half-size / back half-size (m)
	float4 generation = {};            // back angle (rad), mirror, core height, resource flags
	float4 ambientPhase = {};          // reduced extinction, reduced albedo, phase moments
	std::array<float4, 49> msCalibration = {};
	std::array<CloudStormParcel, 8> parcels = {};
	float4 plumeLower = {}, plumeUpper = {};
};
static_assert(sizeof(CloudStormData) == 1824);

/// World-placed storm instances and supercell NDF generation.
struct CloudStormManager
{
	void LoadInstances();
	void SetupResources();
	void CompileShaders(Util::AsyncShaderSet& a_shaders);
	void Update(TextureManager& textures, ID3D11ShaderResourceView* pattern, uint64_t noiseRevision, const std::string& worldspace, bool enabled);
	void UpdateMotion(double time, double previousTime, float2 lowWind, float2 highWind, float lowWindAltitude, float highWindAltitude);
	void CommitMotion();
	/// Rebuilds instance scattering calibration only when albedo or phase changes.
	void UpdateLighting(float dropletDiameter);
	void DrawSettings();
	bool Replaces(const RE::BSGeometry* geometry) const;
	/// Conservative low-resolution pixel rectangle, including local light probes.
	std::array<uint32_t, 4> ScreenRect(float bottomZ, uint32_t width, uint32_t height, float2 weatherAltitude, float curvatureRadius) const;
	bool VolcanicOnly() const
	{
		return !active.empty() && std::ranges::all_of(active, [](const auto& storm) { return (static_cast<uint32_t>(storm.generation.w) & 4u) != 0u; });
	}
	uint32_t Count() const { return static_cast<uint32_t>(active.size()); }
	float2 Altitude() const { return altitude; }
	ID3D11ShaderResourceView* Height() const { return height ? height->srv.get() : nullptr; }
	ID3D11ShaderResourceView* Modeling() const { return modeling ? modeling->srv.get() : nullptr; }
	ID3D11ShaderResourceView* Data() const { return data ? data->SRV() : nullptr; }
	/// Constant data for the single-instance view shader, updated with the motion data.
	ID3D11Buffer* SingleData() const { return Count() == 1 && generatorCb ? generatorCb->CB() : nullptr; }
	uint64_t Revision() const { return revision; }

private:
	std::map<std::string, CloudStormSettings> instances;
	std::string selected, key, error;
	uint64_t revision = 0;
	uint64_t lightingRevision = ~uint64_t(0);
	float lightingDiameter = -1.f;
	std::vector<CloudStormData> active;
	struct WindState
	{
		float2 baseOffset = {};
		float influence = 0.f;
		double time = -1.0;
		std::array<CloudStormParcel, 8> history = {};
	};
	std::vector<WindState> wind;
	void UpdatePlume(CloudStormData& instance, WindState& motion, float2 lowWind, float2 highWind, float lowAltitude, float highAltitude, double time);
	std::vector<RE::TESObjectREFR*> references;
	std::vector<RE::BSFixedString> replacementNodes;
	std::vector<std::string> replacementTextures;
	float2 altitude = {};
	eastl::unique_ptr<ConstantBuffer> generatorCb;
	eastl::unique_ptr<StructuredBuffer> data;
	eastl::unique_ptr<Texture2D> height, modeling;
	std::vector<std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 2>> targets;
	winrt::com_ptr<ID3D11ComputeShader> program;
	winrt::com_ptr<ID3D11SamplerState> sampler;
};
