#pragma once

#include <DirectXMath.h>

#include "Buffer.h"

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

	// Directional shadow data — uploaded every frame to t19.
	// Must match VolumetricShadows::DirectionalShadowData in VolumetricShadows.hlsli exactly.
	struct alignas(16) DirectionalShadowData
	{
		DirectX::XMFLOAT2   EndSplitDistances;    // cascade end depths:   x = cascade 0, y = cascade 1
		DirectX::XMFLOAT2   StartSplitDistances;  // cascade start depths: x = cascade 0, y = cascade 1
		DirectX::XMFLOAT4X4 ShadowMapProj[2];     // world-to-shadow projections for each cascade

		// Dispatch table: maps game shadow-light slot (0..TotalCount-1) to typed buffer index.
		uint32_t LightIsParaboloid[4];  // per game slot: 0 = frustum/spot, 1 = paraboloid
		uint32_t TypedIndex[4];         // per game slot: index within typed buffer
		uint32_t TotalCount;            // total active shadow lights (0..4)
		uint32_t FrustumCount;          // elements written to FrustumShadows (t22)
		uint32_t ParaboloidCount;       // elements written to ParaboloidShadows (t27)
		float    _pad;
	};
	STATIC_ASSERT_ALIGNAS_16(DirectionalShadowData);

	// Frustum (spot) shadow light data — uploaded every frame to t22, one element per active light.
	// Must match FrustumShadowData in ShadowSampling.hlsli exactly.
	struct FrustumShadowData
	{
		DirectX::XMFLOAT4X4 Proj;  // world-to-shadow projection
	};
	STATIC_ASSERT_ALIGNAS_16(FrustumShadowData);

	// Paraboloid (point) shadow light data — uploaded every frame to t27, one element per active light.
	// Must match ParaboloidShadowData in ShadowSampling.hlsli exactly.
	struct ParaboloidShadowData
	{
		DirectX::XMFLOAT4X4 FrontProj;  // world-to-shadow for front hemisphere
		DirectX::XMFLOAT4X4 BackProj;   // world-to-shadow for back hemisphere
		uint32_t             HasBack;    // 1 if back hemisphere depth map is bound
		float                _pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(ParaboloidShadowData);

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

	// Reads shadow parameters from game structs and uploads to three typed structured buffers.
	//   t19 — DirectionalShadowData  (cascade splits + projections + dispatch table)
	//   t20/t21 — directional cascade raw depth maps
	//   t22 — FrustumShadows (spot light projection matrices, max 4 elements)
	//   t23-t26 — frustum light depth maps
	//   t27 — ParaboloidShadows (dual-hemisphere projection matrices, max 4 elements)
	//   t28-t31 — paraboloid front hemisphere depth maps
	//   t32-t35 — paraboloid back hemisphere depth maps
	// Called during EarlyPrepasses immediately after shadow maps have been rendered.
	void CopyShadowData();

	ID3D11ComputeShader* GetComputeMainComposite();
	ID3D11ComputeShader* GetComputeMainCompositeInterior();

	ID3D11BlendState* deferredBlendStates[7][2][13][2];
	ID3D11BlendState* forwardBlendStates[7][2][13][2];

	RE::RENDER_TARGET forwardRenderTargets[4];

	ID3D11ComputeShader* mainCompositeCS = nullptr;
	ID3D11ComputeShader* mainCompositeInteriorCS = nullptr;

	// Directional shadow structured buffer (t19): cascade data + dispatch table.
	Buffer* perDirectionalShadow = nullptr;
	// Frustum (spot) shadow light structured buffer (t22): projection matrices, up to 4 elements.
	Buffer* perFrustumShadows = nullptr;
	// Paraboloid shadow light structured buffer (t27): dual-hemisphere projections, up to 4 elements.
	Buffer* perParaboloidShadows = nullptr;

	bool deferredPass = false;

	ID3D11SamplerState* linearSampler = nullptr;
	ID3D11SamplerState* pointSampler = nullptr;

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