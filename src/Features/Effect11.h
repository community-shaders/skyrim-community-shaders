#pragma once

#include "Buffer.h"
#include "Effect11/ParticleLights.h"
#include "LightLimitFix.h"

#include <memory>
#include <mutex>
#include <winrt/base.h>

struct Effect11 : Feature
{
public:
	virtual inline std::string GetName() override { return "Effect11"; }
	virtual inline std::string GetShortName() override { return "Effect11"; }
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Effect11 provides a framework for loading and executing ENBSeries-compatible FX effect files.\n"
			"This allows for advanced post-processing effects and visual enhancements using DirectX 11 Effect (.fx) files.",
			{ "ENBSeries-compatible FX support",
				"DirectX 11 Effect file loading",
				"Advanced post-processing pipeline",
				"Custom technique execution",
				"Dynamic UI variable system",
				"Support for particle lights" }
		};
	}

	struct alignas(16) PerFrame
	{
		uint Enable;
		uint EnableSky;
		float ColorPow;
		float LightSpriteIntensity;

		float CloudsCurve;
		float CloudsDesaturation;
		float CloudsEdgeIntensity;
		float CloudsEdgeMoonMultiplier;

		float VolumetricRaysDesaturation;
		float3 VolumetricRaysColorFilter;

		uint UseProceduralGradientWeights;
		float ProceduralGradientWeightCurve;
		uint EnableProceduralSun;
		float ProceduralSunDiskRadiusSq;

		float ProceduralSunDiskEdgeScale;
		float ProceduralSunGlowIntensity;
		float ProceduralSunCoronaFalloff;
		float ProceduralSunCoronaScale;

		float ParticleIntensity;
		float ParticleLightingInfluence;
		float ParticleAmbientInfluence;
		float ParticlePointLightingInfluence;

		uint EnableCloudsScattering;
		uint EnableCloudsLightingFromMoon;
		float SkyScatteringIntensity;
		float SkyScatteringAmount;

		float3 SkyScatteringColor;
		float SkyScatteringColorFromSun;

		float SkyScatteringCloudsLightingSunMultiplier;
		float SkyScatteringCloudsLightingMoonIntensity;

		uint EnableVolumetricRays;
		float VolumetricRaysIntensity;
		float VolumetricRaysExtinction;
		float VolumetricRaysSkyColorAmount;

		uint EnableRain;
		float RainMotionStretch;
		float RainMotionTransparency;
		float FireIntensity;
		float FireCurve;
		uint pad0;
	};

	bool enableEffect = false;

	ID3D11PixelShader* raymarchVolumetricRaysPS = nullptr;
	ID3D11PixelShader* applyVolumetricRaysPS = nullptr;
	ID3D11ComputeShader* blurHCS = nullptr;
	ID3D11ComputeShader* blurVCS = nullptr;
	ID3D11BlendState* additiveBlendState = nullptr;
	ID3D11BlendState* alphaBlendState = nullptr;

	std::unique_ptr<Texture2D> vlTexA;
	std::unique_ptr<Texture2D> vlTexB;
	std::unique_ptr<Texture2D> vlDepthHalf;
	std::unique_ptr<ConstantBuffer> vlBlurCB;

	winrt::com_ptr<ID3D11Texture2D> raindropTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> raindropSRV;
	std::string raindropStatus;
	void LoadRaindropTexture();

	PerFrame GetCommonBufferData();

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void Prepass() override;
	virtual void ClearShaderCache() override;
	virtual void PostPostLoad() override;

	void DrawVolumetricRays();

	void OnSkyUpdateColors(RE::Sky* a_sky);
	void OverrideWeather(RE::Sky* a_sky);
	void CheckCommonData();
	void OverridePointLightColor(float3& a_color);

	struct DirectionalAmbientColors
	{
		RE::NiColor directionalAmbientColors[3][2];
	};
	void OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors);

	void ModifySky(RE::BSRenderPass* Pass);
	__declspec(noinline) void ModifyParticle(RE::BSRenderPass* Pass);
	void ParticleShaderHacks();
	bool HandleTonemapRender(RE::RENDER_TARGET a_input, RE::RENDER_TARGET a_output);

	// --- Particle Lights ---

	struct ResolvedParticleLight
	{
		RE::NiPoint3 position;
		RE::NiColorA color;
		float radius;
	};

	struct VertexColorCacheEntry
	{
		bool valid = false;
		bool applyEffectMaterialTint = true;
		Effect11PL::Config config{};
		bool hasGradientConfig = false;
		Effect11PL::GradientConfig gradientConfig{};
		RE::NiColorA baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::uint64_t configVersion = 0;
	};

	struct ParticleLightSettings
	{
		bool EnableParticleLights = true;
		bool EnableParticleLightsCulling = true;
		float ParticleLightsSaturation = 1.0f;
		float BillboardBrightness = 1.0f;
		float BillboardRadius = 1.0f;
		float MaxParticleDistance = 6000.0f;
	};

	ParticleLightSettings particleLightSettings;
	Effect11PL::ConfigStore particleLightConfigs;

	eastl::hash_map<RE::BSGeometry*, VertexColorCacheEntry> vertexColorCache;
	eastl::vector<ResolvedParticleLight> queuedParticleLights;
	eastl::vector<ResolvedParticleLight> currentParticleLights;
	std::mutex particleLightsMutex;

	bool CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique);
	void AddParticleLightsToBuffer(eastl::vector<LightLimitFix::LightData>& a_lightsData, RE::NiPoint3 a_eyePosition);
	void CleanupVertexColorCache(RE::NiNode* a_node);

private:
	VertexColorCacheEntry GetParticleLightConfig(RE::BSRenderPass* a_pass);
	bool QueueParticleLight(RE::BSRenderPass* a_pass, VertexColorCacheEntry& a_reference);

	struct Hooks
	{
		template <int N>
		struct BSBatchRenderer_RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		using RenderPass1 = BSBatchRenderer_RenderPassImmediately<1>;
		using RenderPass2 = BSBatchRenderer_RenderPassImmediately<2>;
		using RenderPass3 = BSBatchRenderer_RenderPassImmediately<3>;

		struct NiNode_Destroy
		{
			static void thunk(RE::NiNode* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};
};
