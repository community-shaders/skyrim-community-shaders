#pragma once

#include <DirectXMath.h>

#include "Buffer.h"

#include "RE/B/BSShadowLight.h"

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


	struct alignas(16) DirectionalShadowData
	{
		DirectX::XMFLOAT4X4 ShadowProj[2];
		DirectX::XMFLOAT4X4 InvShadowProj[2];

		DirectX::XMFLOAT2   EndSplitDistances;
		DirectX::XMFLOAT2   StartSplitDistances;
	};
	STATIC_ASSERT_ALIGNAS_16(DirectionalShadowData);

	struct alignas(16) ShadowData
	{
		DirectX::XMFLOAT4X4  ShadowProj;
		DirectX::XMFLOAT4X4  InvShadowProj;

		float4				 ShadowParam;
	};

	STATIC_ASSERT_ALIGNAS_16(ShadowData);

	void SetupResources();
	void ReflectionsPrepasses();
	void EarlyPrepasses();
	void StartDeferred();
	void OverrideBlendStates();
	void ResetBlendStates();
	void DeferredPasses();
	void EndDeferred();

	void PrepassPasses();

	void ClearShaderCache();

	// Reads shadow parameters from game structs and uploads to structured buffers and texture arrays.
	//   t81 — DirectionalShadowData  (cascade splits + world-to-shadow projections)
	//   t82 — DirectionalShadowCascades  (Texture2DArray, game's kSHADOWMAPS_ESRAM depth SRV)
	//   t83 — ShadowData (unified shadow light data, up to 4 elements)
	//   t84 — ShadowMaps (Texture2DArray, game's kSHADOWMAPS depth SRV, bound directly)
	// Called during EarlyPrepasses immediately after shadow maps have been rendered.
	void CopyShadowData();

	ID3D11ComputeShader* GetComputeMainComposite();
	ID3D11ComputeShader* GetComputeMainCompositeInterior();

	ID3D11BlendState* deferredBlendStates[7][2][13][2];
	ID3D11BlendState* forwardBlendStates[7][2][13][2];

	RE::RENDER_TARGET forwardRenderTargets[4];

	ID3D11ComputeShader* mainCompositeCS = nullptr;
	ID3D11ComputeShader* mainCompositeInteriorCS = nullptr;

	// Directional shadow structured buffer (t81): cascade splits and projections.
	Buffer* perDirectionalShadow = nullptr;
	// Unified shadow light structured buffer (t83): projection + type for each active light.
	Buffer* perShadows = nullptr;

	bool deferredPass = false;

	ID3D11SamplerState* linearSampler   = nullptr;
	ID3D11SamplerState* pointSampler    = nullptr;
	ID3D11SamplerState* shadowCmpSampler = nullptr;  // PCF comparison sampler (s14)

private:
	void SetShadowCascadeParameters(RE::BSShadowLight::RUNTIME_DATA& lightData, DirectionalShadowData& dd);
	void SetShadowCascadeParameters(RE::BSShadowLight::RUNTIME_DATA_VR& lightData, DirectionalShadowData& dd);

	void SetShadowParameters(RE::BSShadowLight::RUNTIME_DATA& lightData, ShadowData& sd);
	void SetShadowParameters(RE::BSShadowLight::RUNTIME_DATA_VR& lightData, ShadowData& sd);

public:
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

		struct BSCubeMapCamera_RenderCubemap
		{
			static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderFirstPersonView
		{
			static void thunk(bool a1, bool a2);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_ResetState
		{
			static void thunk(void* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

			stl::write_thunk_call<Main_RenderShadowMaps>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x2EC, 0x2EC, 0x248));

			stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841, 0x791));
			stl::write_thunk_call<Main_RenderWorld_Start>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x8E, 0x84));
			stl::write_thunk_call<Main_RenderWorld_BlendedDecals>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x319, 0x308, 0x321));

			if (!REL::Module::IsVR())
				stl::write_thunk_call<Main_RenderFirstPersonView>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x944, 0x954));

			stl::detour_thunk<Renderer_ResetState>(REL::RelocationID(75570, 77371));

			logger::info("[Deferred] Installed hooks");
		}
	};
};