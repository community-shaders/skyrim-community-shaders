#pragma once

#define ALBEDO RE::RENDER_TARGETS::kINDIRECT
#define SPECULAR RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED
#define REFLECTANCE RE::RENDER_TARGETS::kRAWINDIRECT
#define NORMALROUGHNESS RE::RENDER_TARGETS::kRAWINDIRECT_DOWNSCALED
#define MASKS RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS
#define MASKS2 RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS_DOWNSCALED

class Deferred
{
public:
	static Deferred* GetSingleton()
	{
		static Deferred singleton;
		return &singleton;
	}

	void SetupResources();
	void CopyShadowData();
	void ReflectionsPrepasses();
	void EarlyPrepasses();
	void StartDeferred();
	void OverrideBlendStates();
	void ResetBlendStates();
	void DeferredPasses();
	void EndDeferred();

	void PrepassPasses();

	void ClearShaderCache();
	ID3D11ComputeShader* GetComputeAmbientComposite();
	ID3D11ComputeShader* GetComputeAmbientCompositeInterior();
	ID3D11ComputeShader* GetComputeMainComposite();

	ID3D11ComputeShader* GetComputeMainCompositeInterior();

	ID3D11BlendState* deferredBlendStates[7][2][13][2];
	ID3D11BlendState* forwardBlendStates[7][2][13][2];

	RE::RENDER_TARGET forwardRenderTargets[4];

	ID3D11ComputeShader* ambientCompositeCS = nullptr;
	ID3D11ComputeShader* ambientCompositeInteriorCS = nullptr;

	ID3D11ComputeShader* mainCompositeCS = nullptr;
	ID3D11ComputeShader* mainCompositeInteriorCS = nullptr;

	bool inWorld = false;
	bool inBlendedDecals = false;
	bool inDecals = false;
	bool inReflections = false;
	bool deferredPass = false;

	Texture2D* prevDiffuseAmbientTexture = nullptr;

	ID3D11SamplerState* linearSampler = nullptr;

	struct alignas(16) PerGeometry
	{
		float4 VPOSOffset;
		float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
		float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
		float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
		float4 FocusShadowFadeParam;
		float4 DebugColor;
		float4 PropertyColor;
		float4 AlphaTestRef;
		float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
		DirectX::XMFLOAT4X3 FocusShadowMapProj[4];
		// Since PerGeometry is passed between c++ and hlsl, can't have different defines due to strong typing
		DirectX::XMFLOAT4X3 ShadowMapProj[2][3];
		DirectX::XMFLOAT4X3 CameraViewProjInverse[2];
	};

	ID3D11ComputeShader* copyShadowCS = nullptr;
	Buffer* perShadow = nullptr;
	ID3D11ShaderResourceView* shadowView = nullptr;

	struct Hooks
	{
		struct Main_RenderShadowMaps
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk(bool a1);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_Start
		{
			static void thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_BlendedDecals
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderAccumulator_BlendedDecals_RenderGeometryGroup
		{
			static void thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderAccumulator_FirstPerson_BlendedDecals
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderAccumulator_ShadowMapOrMask_BlendedDecals
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSCubeMapCamera_RenderCubemap
		{
			static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

			stl::write_thunk_call<Main_RenderShadowMaps>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x2EC, 0x2EC, 0x248));

			stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841, 0x791));
			stl::write_thunk_call<Main_RenderWorld_Start>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x8E, 0x84));
			stl::write_thunk_call<Main_RenderWorld_BlendedDecals>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x319, 0x308, 0x321));

			stl::write_thunk_call<BSShaderAccumulator_BlendedDecals_RenderGeometryGroup>(REL::RelocationID(99942, 106587).address() + REL::Relocate(0x111, 0x112));

			stl::write_thunk_call<BSShaderAccumulator_FirstPerson_BlendedDecals>(REL::RelocationID(99943, 106588).address() + REL::Relocate(0xFE, 0xF4));
			stl::write_thunk_call<BSShaderAccumulator_ShadowMapOrMask_BlendedDecals>(REL::RelocationID(99947, 106592).address() + 0x107);

			logger::info("[Deferred] Installed hooks");
		}
	};
};