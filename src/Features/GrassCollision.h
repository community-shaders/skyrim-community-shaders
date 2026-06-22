#pragma once

#include "Buffer.h"

struct GrassCollision : Feature
{
public:
	virtual inline std::string GetName() override { return "Grass Collision"; }
	virtual std::string GetDisplayName() override { return T("feature.grass_collision.name", "Grass Collision"); }
	virtual inline std::string GetShortName() override { return "GrassCollision"; }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_COLLISION"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kGrass; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.grass_collision.description", "Enables dynamic grass interactions where grass bends and moves in response to actors walking through it, creating more immersive environmental reactions."),
			{ T("feature.grass_collision.key_feature_1", "Real-time grass deformation from actor movement"),
				T("feature.grass_collision.key_feature_2", "Collision detection for up to 256 simultaneous interactions"),
				T("feature.grass_collision.key_feature_3", "Dynamic tracking of actor positions for grass response"),
				T("feature.grass_collision.key_feature_4", "Performance-optimized collision calculation"),
				T("feature.grass_collision.key_feature_5", "Seamless integration with existing grass rendering") } };
	};

	/** @brief Returns true only for Grass shader type. */
	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	/** @brief Dispatches the collision update compute shader to write actor collision data into the collision texture. */
	void UpdateCollisionTexture();

	struct Settings
	{
		bool EnableGrassCollision = 1;
		bool TrackRagdolls = 1;
		bool EnableBlur = 1;
	};

	struct alignas(16) BoundingBoxPacked
	{
		float2 MinExtent = { 0, 0 };
		float2 MaxExtent = { 0, 0 };
		uint IndexStart = 0;
		uint IndexEnd = 0;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(BoundingBoxPacked);

	struct alignas(16) PerFrame
	{
		float2 PosOffset;              // cell origin in camera model space
		DirectX::XMUINT2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

		DirectX::XMINT2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;

		float CameraHeightDelta;
		float3 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	Settings settings;

	ConstantBuffer* perFrame = nullptr;

	eastl::unique_ptr<Buffer> collisionBoundingBoxes = nullptr;
	eastl::unique_ptr<Buffer> collisionInstances = nullptr;

	eastl::vector<BoundingBoxPacked> queuedBoundingBoxes;
	eastl::vector<float4> queuedCollisions;

	/** @brief Releases the cached collision update compute shader so it can be recompiled. */
	virtual void ClearShaderCache() override;

	/** @brief Returns the collision update compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetCollisionUpdateCS();
	ID3D11ComputeShader* collisionUpdateCS;

	Texture2D* collisionTexture = nullptr;

	/** @brief Creates the collision texture, structured buffers for bounding boxes and collision instances. */
	virtual void SetupResources() override;

	/** @brief Draws the ImGui settings UI for grass collision options. */
	virtual void DrawSettings() override;
	/**
	 * @brief Gathers collision shapes from nearby actors and queues them for GPU upload.
	 *
	 * Sorted by distance, limited to MAX_BOUNDING_BOXES actors with up to
	 * MAX_COLLISIONS_PER_BOUNDING_BOX shapes each.
	 */
	void QueueCollisions();
	/**
	 * @brief Uploads queued collision data to GPU buffers and dispatches the collision texture update.
	 *
	 * Called once per frame from the grass shader setup geometry hook.
	 */
	void Update();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Installs the BSGrassShader and main update hooks after all plugins have loaded. */
	virtual void PostPostLoad() override;


	struct Hooks
	{
		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct MainUpdate_QueueCollisions
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);
			stl::write_thunk_call<MainUpdate_QueueCollisions>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x748, 0xC26));
			logger::info("[GRASS COLLISION] Installed hooks");
		}
	};
};
