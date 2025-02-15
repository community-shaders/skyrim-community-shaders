#pragma once

struct GrassCollision : Feature
{
	static GrassCollision* GetSingleton()
	{
		static GrassCollision singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Grass Collision"; }
	virtual inline std::string GetShortName() override { return "GrassCollision"; }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_COLLISION"; }

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct Settings
	{
		bool EnableGrassCollision = 1;
	};

	struct alignas(16) CollisionData
	{
		float4 centre[2];
	};

	struct alignas(16) PerFrame
	{
		CollisionData collisionData[256];
		uint numCollisions;
		uint pad0[3];
	};

	std::uint32_t totalActorCount = 0;
	std::uint32_t activeActorCount = 0;
	std::uint32_t currentCollisionCount = 0;
	std::vector<RE::Actor*> actorList{};
	std::uint32_t colllisionCount = 0;

	Settings settings;

	bool updatePerFrame = false;
	ConstantBuffer* perFrame = nullptr;
	int eyeCount = !REL::Module::IsVR() ? 1 : 2;

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void DrawSettings() override;
	void UpdateCollisions(PerFrame& perFrame);
	void Update();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void PostPostLoad() override;

	virtual bool SupportsVR() override { return true; };

	struct Hooks
	{
		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);
			logger::info("[GRASS COLLISION] Installed hooks");
		}
	};
};
