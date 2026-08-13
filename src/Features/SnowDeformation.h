#pragma once

#include "Buffer.h"

struct SnowDeformation : Feature
{
public:
	virtual inline std::string GetName() override { return "Snow Deformation"; }
	virtual std::string GetDisplayName() override { return T("feature.snow_deformation.name", "Snow Deformation"); }
	virtual inline std::string GetShortName() override { return "SnowDeformation"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SNOW_DEFORMATION"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief The lighting shader samples the deformation map on landscape draws. */
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::Lighting;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.snow_deformation.description", "Maintains a persistent deformation map around the player so snow can be visibly compressed by actors moving through it, leaving lasting trails."),
			{ T("feature.snow_deformation.key_feature_1", "Persistent world-space deformation map following the player"),
				T("feature.snow_deformation.key_feature_2", "Trails carved by the player, NPCs and creatures"),
				T("feature.snow_deformation.key_feature_3", "Configurable snow refill over time"),
				T("feature.snow_deformation.key_feature_4", "Compute-shader based, low performance impact") } };
	};

	// The deformation map is a square world-space window that follows the camera
	// in whole-texel steps. Values are normalized depression depth: 0 = untouched
	// snow, 1 = compressed to the ground. The window's WORLD size is runtime
	// (deformWorldSize, driven by the Trenches range slider); the texture
	// resolution is fixed, so trench detail coarsens with range.
	static constexpr uint kTextureDim = 2048;
	static constexpr uint kMaxStamps = 128;

	/** @brief Skyrim world units per meter (1 unit ≈ 1.43 cm). Range sliders are in meters. */
	static constexpr float kUnitsPerMeter = 70.0f;

	struct Settings
	{
		bool EnableSnowDeformation = true;
		bool ShowDebugTexture = false;
		/** @brief Scale on Havok collision-shape radii (20 = 1.0x = the shapes' actual size). */
		float StampRadius = 20.0f;
		/** @brief Seconds for compressed snow to fully recover. 0 disables refilling. */
		float RefillTime = 700.0f;
		/** @brief Render distances in METERS (converted via kUnitsPerMeter). Shell scales the warped grid's spacing (CB-only, applies live); Trenches resizes the deformation window (the map clears on apply — content is scale-relative); Object Snow is the statics capture cutoff. */
		float RangeTrenchesM = 100.0f;
	};

	/** @brief GPU-side settings, appended to the shared FeatureData cbuffer (b6). Layout must match SnowDeformationSettings in SharedData.hlsli. */
	struct alignas(16) SettingsGPU
	{
		float2 WindowOrigin;
		float InvWorldSize;
		uint EnableSnowDeformation;

		uint DebugTerrainOverlay;
		float3 padSnow;
	};
	STATIC_ASSERT_ALIGNAS_16(SettingsGPU);

	/**
	 * @brief Returns this frame's GPU settings for the shared FeatureData buffer.
	 *
	 * Also advances the deformation window when a_inWorld is true: the origin
	 * MUST be computed here (during State::UpdateSharedData, before Prepass)
	 * so the constant buffer and the scrolled texture agree within a frame —
	 * computing it in Prepass makes trails swim on window-scroll frames.
	 */
	SettingsGPU GetCommonBufferData(bool a_inWorld);

	/** @brief Per-dispatch constants for the deformation update. Layout must match PerFrame in DeformationUpdateCS.hlsl. */
	struct alignas(16) PerFrame
	{
		float2 WindowOrigin;
		DirectX::XMINT2 ScrollDelta;

		float TexelSize;
		uint StampCount;
		float RefillAmount;
		uint ClearMap;

		float4 Stamps[kMaxStamps];
		/** @brief Segment start per stamp (the stamped shape's previous position) — stamps are capsules, so trails stay continuous regardless of actor speed. */
		float4 StampEnds[kMaxStamps];
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	Settings settings;

	ConstantBuffer* perFrame = nullptr;
	Texture2D* deformationTextures[2] = { nullptr, nullptr };
	uint currentTexture = 0;

	/** @brief SRV of the most recently written deformation map, for shader sampling and debug UI. */
	ID3D11ShaderResourceView* GetDeformationSRV() const { return deformationTextures[currentTexture]->srv.get(); }
	/** @brief World XY of the corner of texel (0,0) of the current deformation window. */
	float2 GetWindowOrigin() const { return windowOrigin; }

	/** @brief Creates the ping-pong deformation textures and the per-frame constant buffer. */
	virtual void SetupResources() override;

	/**
	 * @brief Per-frame update: gathers actor stamp positions, scrolls the window
	 * to follow the camera, and dispatches the deformation update compute shader.
	 */
	virtual void Prepass() override;

	/** @brief Returns the deformation update compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetDeformationUpdateCS();
	ID3D11ComputeShader* deformationUpdateCS = nullptr;
	virtual void ClearShaderCache() override;

	/** @brief Draws the ImGui settings UI, including the debug view of the deformation map. Implemented in SnowDeformation/Menu.cpp. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Installs both landscape hooks. Implemented in SnowDeformation/TerrainData.cpp. The TESObjectLAND detour is installed here (after TruePBR's, which runs first in the feature list) so ours is OUTER and sees the final — possibly TruePBR-replaced — material on each quad. */
	virtual void PostPostLoad() override;

	/** @brief Caches a "tile is snow material" bitmask per landscape quad material, for the terrain shader's per-tile snow detection. */
	void TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	/** @brief Publishes the cached snow mask for the material about to be drawn via ExtraFeatureDescriptor bits 10-15. */
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);

	/** @brief Diagnostics shown in the settings UI: landscape materials that hit/missed the snow-mask cache at draw time. */
	std::atomic<uint64_t> landMaskHits{ 0 };
	std::atomic<uint64_t> landMaskMisses{ 0 };

	/** @brief Thread-safe size read for the settings UI. */
	size_t snowMasksSizeForUI()
	{
		const std::shared_lock lock(snowMaskMutex);
		return snowMasks.size();
	}

	/** @brief Runtime-only diagnostic toggle; not persisted in settings JSON. */
	bool debugTerrainOverlay = false;

protected:
	/** @brief Fills perFrameData.Stamps from the player and nearby loaded actors. Implemented in SnowDeformation/Stamping.cpp. */
	void GatherStamps(PerFrame& perFrameData);

	std::unordered_map<uintptr_t, uint8_t> snowMasks;
	std::shared_mutex snowMaskMutex;

	float2 windowOrigin = { 0, 0 };
	DirectX::XMINT2 pendingScrollDelta = { 0, 0 };
	bool clearRequested = true;

	// ---- Runtime render-distance state (driven by the Range* settings) ----
	/** @brief Deformation window world size (2x the Trenches range). Changing it invalidates the map (content is scale-relative), so the slider clears on apply. */
	float deformWorldSize = 14000.0f;
	bool trenchRangeDirty = false;
	bool rangeInitApplied = false;

public:
	/** @brief Applies pending range-setting changes (trench window resize + map clear). Called at Prepass start; the first call applies loaded settings. */
	void ApplyRangeSettings();

protected:
	/** @brief Trail history per collision shape: key = (formID << 16) | traversal index. */
	std::unordered_map<uint64_t, float2> stampPrevPositions;

	/** @brief Last 3D-root position per loose inanimate object (formID), rebuilt every frame from the in-range scan. Props carve only while their root MOVES — the cheap position gate runs before any collision traversal, so resting world clutter costs one hash lookup per frame. */
	std::unordered_map<uint32_t, RE::NiPoint3> propPrevPositions;

	/** @brief Stillness latch per corpse (formID). Ragdoll micro-drift accumulates against the FROZEN resting anchors and would eventually cross the movement gate, firing a one-frame trench pulse under an already-buried corpse. Once a corpse has been still long enough it settles: only a large accumulated displacement (real dragging, explosions) wakes it again. Erased when the actor is seen alive (reanimation). */
	struct CorpseRest
	{
		uint16_t stillFrames = 0;
		bool settled = false;
	};
	std::unordered_map<uint32_t, CorpseRest> corpseRestStates;
};
