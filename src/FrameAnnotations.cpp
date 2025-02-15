#include "FrameAnnotations.h"

#include "State.h"

#pragma comment(lib, "dxguid.lib")

namespace FrameAnnotations
{
	template <RE::BSShader::Type ShaderType>
	struct BSShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			if (globals::state->frameAnnotations) {
				const std::string passName = std::format("[{}:{:X}] <{}> {}", magic_enum::enum_name(ShaderType), pass->passEnum,
					pass->accumulationHint, pass->geometry->name.c_str());
				globals::state->BeginPerfEvent(passName);
			}

			func(shader, pass, renderFlags);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <RE::BSShader::Type ShaderType>
	struct BSShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			func(shader, pass, renderFlags);

			if (globals::state->frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
	struct BSImagespaceShader_Render
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			globals::state->BeginPerfEvent(std::format("{} Draw", magic_enum::enum_name(EffectType)));

			func(imageSpaceShader, shape, param);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
	struct BSImagespaceShader_Dispatch
	{
		static void thunk(void* imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
		{
			globals::state->BeginPerfEvent(std::format("{} Dispatch", magic_enum::enum_name(EffectType)));

			func(imageSpaceShader, a1, a2, a3);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_FinishAccumulatingDispatch
	{
		static void thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, uint32_t renderFlags)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent(std::format("BSShaderAccumulator::FinishAccumulatingDispatch [{}] <{}>",
					static_cast<uint32_t>(shaderAccumulator->GetRuntimeData().renderMode), renderFlags));
			}

			func(shaderAccumulator, renderFlags);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSCubeMapCamera_RenderCubemap
	{
		static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
		{
			globals::state->BeginPerfEvent(std::format("Cubemap {}", camera->name.c_str()));

			func(camera, a2, a3, a4, a5);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowDirectionalLight_RenderShadowmaps
	{
		static void thunk(RE::BSShadowLight* light, void* a2)
		{
			globals::state->BeginPerfEvent("Directional Light Shadowmaps");

			func(light, a2);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowFrustumLight_RenderShadowmaps
	{
		static void thunk(RE::BSShadowLight* light, void* a2)
		{
			globals::state->BeginPerfEvent("Spot Light Shadowmaps");

			func(light, a2);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowParabolicLight_RenderShadowmaps
	{
		static void thunk(RE::BSShadowLight* light, void* a2)
		{
			globals::state->BeginPerfEvent("Omnidirectional Light Shadowmaps");

			func(light, a2);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderBatches
	{
		static bool thunk(void* renderer, uint32_t* currentPass, uint32_t* bucketIndex,
			void* passIndexList,
			uint32_t renderFlags)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent(std::format("BSBatchRenderer::RenderBatches ({:X})[{}] <{}>", *currentPass, *bucketIndex,
					renderFlags));
			}

			const bool result = func(renderer, currentPass, bucketIndex, passIndexList, renderFlags);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}

			return result;
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderDepth
	{
		static void thunk(bool a1, bool a2)
		{
			globals::state->BeginPerfEvent("Depth");

			func(a1, a2);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderShadowmasks
	{
		static void thunk(bool a1)
		{
			globals::state->BeginPerfEvent("Shadowmasks");

			func(a1);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderWorld
	{
		static void thunk(bool a1)
		{
			globals::state->BeginPerfEvent("World");

			func(a1);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderFirstPersonView
	{
		static void thunk(bool a1, bool a2)
		{
			globals::state->BeginPerfEvent("First Person View");

			func(a1, a2);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderWaterEffects
	{
		static void thunk()
		{
			globals::state->BeginPerfEvent("Water Effects");

			func();

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPlayerView
	{
		static void thunk(void* a1, bool a2, bool a3)
		{
			globals::state->BeginPerfEvent("Player View");

			func(a1, a2, a3);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_RenderEffects
	{
		static void thunk(void* accumulator, uint32_t renderFlags)
		{
			globals::state->BeginPerfEvent("Effects");

			func(accumulator, renderFlags);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_RenderBatches
	{
		static void thunk(void* shaderAccumulator, uint32_t firstPass, uint32_t lastPass, uint32_t renderFlags, int groupIndex)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent(std::format("BSShaderAccumulator::RenderBatches ({:X}:{:X})[{}] <{}>", firstPass, lastPass, groupIndex,
					renderFlags));
			}

			func(shaderAccumulator, firstPass, lastPass, renderFlags, groupIndex);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_RenderPersistentPassList
	{
		static void thunk(void* passList, uint32_t renderFlags)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent(std::format("BSShaderAccumulator::RenderPersistentPassList <{}>", renderFlags));
			}

			func(passList, renderFlags);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VolumetricLightingDescriptor_Render
	{
		static void thunk(void* a1, void* a2, bool a3)
		{
			globals::state->BeginPerfEvent("Volumetric Lighting");

			func(a1, a2, a3);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void OnPostPostLoad()
	{
		if (!globals::state->frameAnnotations)
			return;

		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(
			RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Effect>>(
			RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Water>>(
			RE::VTABLE_BSWaterShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Utility>>(
			RE::VTABLE_BSUtilityShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Particle>>(
			RE::VTABLE_BSParticleShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Grass>>(
			RE::VTABLE_BSGrassShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::DistantTree>>(
			RE::VTABLE_BSDistantTreeShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::BloodSplatter>>(
			RE::VTABLE_BSBloodSplatterShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Sky>>(
			RE::VTABLE_BSSkyShader[0]);

		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Lighting>>(
			RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Effect>>(
			RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Water>>(
			RE::VTABLE_BSWaterShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Utility>>(
			RE::VTABLE_BSUtilityShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Particle>>(
			RE::VTABLE_BSParticleShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Grass>>(
			RE::VTABLE_BSGrassShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::DistantTree>>(
			RE::VTABLE_BSDistantTreeShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::BloodSplatter>>(
			RE::VTABLE_BSBloodSplatterShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Sky>>(
			RE::VTABLE_BSSkyShader[0]);

		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISFXAA>>(
			RE::VTABLE_BSImagespaceShaderFXAA[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopy>>(
			RE::VTABLE_BSImagespaceShaderCopy[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyDynamicFetchDisabled>>(
			RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyScaleBias>>(
			RE::VTABLE_BSImagespaceShaderCopyScaleBias[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyCustomViewport>>(
			RE::VTABLE_BSImagespaceShaderCopyCustomViewport[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyGrayScale>>(
			RE::VTABLE_BSImagespaceShaderGreyScale[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRefraction>>(
			RE::VTABLE_BSImagespaceShaderRefraction[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDoubleVision>>(
			RE::VTABLE_BSImagespaceShaderDoubleVision[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyTextureMask>>(
			RE::VTABLE_BSImagespaceShaderTextureMask[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISMap>>(
			RE::VTABLE_BSImagespaceShaderMap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWorldMap>>(
			RE::VTABLE_BSImagespaceShaderWorldMap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWorldMapNoSkyBlur>>(
			RE::VTABLE_BSImagespaceShaderWorldMapNoSkyBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfField>>(
			RE::VTABLE_BSImagespaceShaderDepthOfField[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldFogged[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldMaskedFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlur>>(
			RE::VTABLE_BSImagespaceShaderDistantBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlurFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurFogged[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlurMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurMaskedFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlur>>(
			RE::VTABLE_BSImagespaceShaderRadialBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlurMedium>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurMedium[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlurHigh>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurHigh[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRTonemapBlendCinematic>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16Lum[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4RGB2Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4RGB2Lum[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LumClamp[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LightAdapt[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LumClamp[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LightAdapt[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur3>>(
			RE::VTABLE_BSImagespaceShaderBlur3[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur5>>(
			RE::VTABLE_BSImagespaceShaderBlur5[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur7>>(
			RE::VTABLE_BSImagespaceShaderBlur7[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur9>>(
			RE::VTABLE_BSImagespaceShaderBlur9[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur11>>(
			RE::VTABLE_BSImagespaceShaderBlur11[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur13>>(
			RE::VTABLE_BSImagespaceShaderBlur13[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur15>>(
			RE::VTABLE_BSImagespaceShaderBlur15[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur3>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur3[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur5>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur5[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur7>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur7[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur9>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur9[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur11>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur11[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur13>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur13[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur15>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur15[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur3>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur3[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur5>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur5[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur7>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur7[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur9>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur9[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur11>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur11[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur13>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur13[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur15>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur15[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementClearSimulation>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementClearSimulation[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementTexOffset>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementTexOffset[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementWadingRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementWadingRipple[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementRainRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementRainRipple[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterWadingHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterWadingHeightmap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterRainHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterRainHeightmap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterBlendHeightmaps>>(
			RE::VTABLE_BSImagespaceShaderWaterBlendHeightmaps[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterSmoothHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterSmoothHeightmap[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementNormals>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementNormals[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNoiseScrollAndBlend>>(
			RE::VTABLE_BSImagespaceShaderNoiseScrollAndBlend[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNoiseNormalmap>>(
			RE::VTABLE_BSImagespaceShaderNoiseNormalmap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLocalMap>>(
			RE::VTABLE_BSImagespaceShaderLocalMap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISAlphaBlend>>(
			RE::VTABLE_BSImagespaceShaderAlphaBlend[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlare>>(
			RE::VTABLE_BSImagespaceShaderLensFlare[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlareVisibility>>(
			RE::VTABLE_BSImagespaceShaderLensFlareVisibility[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISApplyReflections>>(
			RE::VTABLE_BSImagespaceShaderApplyReflections[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISApplyVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISApplyVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBasicCopy>>(
			RE::VTABLE_BSImagespaceShaderISBasicCopy[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur>>(
			RE::VTABLE_BSImagespaceShaderISBlur[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISVolumetricLightingBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurHCS[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISVolumetricLightingBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISParallaxMaskBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISParallaxMaskBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurVCS[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISCompositeVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCompositeLensFlare>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlare[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<
								  RE::ImageSpaceManager::ISCompositeLensFlareVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlareVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopySubRegionCS>>(
			RE::VTABLE_BSImagespaceShaderISCopySubRegionCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDebugSnow>>(
			RE::VTABLE_BSImagespaceShaderISDebugSnow[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsample>>(
			RE::VTABLE_BSImagespaceShaderISDownsample[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleIgnoreBrightest>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightest[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleCS[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleIgnoreBrightestCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightestCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISExp>>(
			RE::VTABLE_BSImagespaceShaderISExp[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISIBLensFlares>>(
			RE::VTABLE_BSImagespaceShaderISIBLensFlares[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLightingComposite>>(
			RE::VTABLE_BSImagespaceShaderISLightingComposite[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<
								  RE::ImageSpaceManager::ISLightingCompositeNoDirectionalLight>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeNoDirectionalLight[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLightingCompositeMenu>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeMenu[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISPerlinNoiseCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoiseCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISPerlinNoise2DCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoise2DCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionsRayTracing>>(
			RE::VTABLE_BSImagespaceShaderReflectionsRayTracing[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionsDebugSpecMask>>(
			RE::VTABLE_BSImagespaceShaderReflectionsDebugSpecMask[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurH>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurH[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurV>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurV[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCameraZ>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZ[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCameraZAndMipsCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZAndMipsCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeSAO>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAO[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeFog[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeSAOFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAOFog[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISMinify>>(
			RE::VTABLE_BSImagespaceShaderISMinify[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISMinifyContrast>>(
			RE::VTABLE_BSImagespaceShaderISMinifyContrast[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAORawAO>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAO[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAORawAONoTemporal>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAONoTemporal[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAORawAOCS>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAOCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSILComposite>>(
			RE::VTABLE_BSImagespaceShaderISSILComposite[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSILRawInd>>(
			RE::VTABLE_BSImagespaceShaderISSILRawInd[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSimpleColor>>(
			RE::VTABLE_BSImagespaceShaderISSimpleColor[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDisplayDepth>>(
			RE::VTABLE_BSImagespaceShaderISDisplayDepth[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSnowSSS>>(
			RE::VTABLE_BSImagespaceShaderISSnowSSS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTemporalAA>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTemporalAA_UI>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_UI[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTemporalAA_Water>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_Water[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISUpsampleDynamicResolution>>(
			RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterBlend>>(
			RE::VTABLE_BSImagespaceShaderISWaterBlend[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISUnderwaterMask>>(
			RE::VTABLE_BSImagespaceShaderISUnderwaterMask[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterFlow>>(
			RE::VTABLE_BSImagespaceShaderWaterFlow[3]);

		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISFXAA>>(
			RE::VTABLE_BSImagespaceShaderFXAA[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopy>>(
			RE::VTABLE_BSImagespaceShaderCopy[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyDynamicFetchDisabled>>(
			RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyScaleBias>>(
			RE::VTABLE_BSImagespaceShaderCopyScaleBias[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyCustomViewport>>(
			RE::VTABLE_BSImagespaceShaderCopyCustomViewport[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyGrayScale>>(
			RE::VTABLE_BSImagespaceShaderGreyScale[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRefraction>>(
			RE::VTABLE_BSImagespaceShaderRefraction[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDoubleVision>>(
			RE::VTABLE_BSImagespaceShaderDoubleVision[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyTextureMask>>(
			RE::VTABLE_BSImagespaceShaderTextureMask[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISMap>>(
			RE::VTABLE_BSImagespaceShaderMap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWorldMap>>(
			RE::VTABLE_BSImagespaceShaderWorldMap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWorldMapNoSkyBlur>>(
			RE::VTABLE_BSImagespaceShaderWorldMapNoSkyBlur[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfField>>(
			RE::VTABLE_BSImagespaceShaderDepthOfField[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldFogged[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldMaskedFogged[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDistantBlur>>(
			RE::VTABLE_BSImagespaceShaderDistantBlur[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDistantBlurFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurFogged[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDistantBlurMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurMaskedFogged[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRadialBlur>>(
			RE::VTABLE_BSImagespaceShaderRadialBlur[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRadialBlurMedium>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurMedium[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRadialBlurHigh>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurHigh[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRTonemapBlendCinematic>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16Lum[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4RGB2Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4RGB2Lum[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LumClamp[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LightAdapt[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LumClamp[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LightAdapt[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur3>>(
			RE::VTABLE_BSImagespaceShaderBlur3[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur5>>(
			RE::VTABLE_BSImagespaceShaderBlur5[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur7>>(
			RE::VTABLE_BSImagespaceShaderBlur7[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur9>>(
			RE::VTABLE_BSImagespaceShaderBlur9[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur11>>(
			RE::VTABLE_BSImagespaceShaderBlur11[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur13>>(
			RE::VTABLE_BSImagespaceShaderBlur13[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur15>>(
			RE::VTABLE_BSImagespaceShaderBlur15[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur3>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur3[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur5>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur5[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur7>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur7[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur9>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur9[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur11>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur11[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur13>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur13[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur15>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur15[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur3>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur3[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur5>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur5[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur7>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur7[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur9>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur9[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur11>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur11[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur13>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur13[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur15>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur15[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementClearSimulation>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementClearSimulation[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementTexOffset>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementTexOffset[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementWadingRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementWadingRipple[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementRainRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementRainRipple[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterWadingHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterWadingHeightmap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterRainHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterRainHeightmap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterBlendHeightmaps>>(
			RE::VTABLE_BSImagespaceShaderWaterBlendHeightmaps[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterSmoothHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterSmoothHeightmap[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementNormals>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementNormals[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNoiseScrollAndBlend>>(
			RE::VTABLE_BSImagespaceShaderNoiseScrollAndBlend[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNoiseNormalmap>>(
			RE::VTABLE_BSImagespaceShaderNoiseNormalmap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLocalMap>>(
			RE::VTABLE_BSImagespaceShaderLocalMap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISAlphaBlend>>(
			RE::VTABLE_BSImagespaceShaderAlphaBlend[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLensFlare>>(
			RE::VTABLE_BSImagespaceShaderLensFlare[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLensFlareVisibility>>(
			RE::VTABLE_BSImagespaceShaderLensFlareVisibility[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISApplyReflections>>(
			RE::VTABLE_BSImagespaceShaderApplyReflections[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISApplyVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISApplyVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBasicCopy>>(
			RE::VTABLE_BSImagespaceShaderISBasicCopy[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur>>(
			RE::VTABLE_BSImagespaceShaderISBlur[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISVolumetricLightingBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurHCS[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISVolumetricLightingBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISParallaxMaskBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISParallaxMaskBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurVCS[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCompositeVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCompositeLensFlare>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlare[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<
								  RE::ImageSpaceManager::ISCompositeLensFlareVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlareVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopySubRegionCS>>(
			RE::VTABLE_BSImagespaceShaderISCopySubRegionCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDebugSnow>>(
			RE::VTABLE_BSImagespaceShaderISDebugSnow[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsample>>(
			RE::VTABLE_BSImagespaceShaderISDownsample[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleIgnoreBrightest>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightest[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleCS[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleIgnoreBrightestCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightestCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISExp>>(
			RE::VTABLE_BSImagespaceShaderISExp[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISIBLensFlares>>(
			RE::VTABLE_BSImagespaceShaderISIBLensFlares[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLightingComposite>>(
			RE::VTABLE_BSImagespaceShaderISLightingComposite[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<
								  RE::ImageSpaceManager::ISLightingCompositeNoDirectionalLight>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeNoDirectionalLight[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLightingCompositeMenu>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeMenu[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISPerlinNoiseCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoiseCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISPerlinNoise2DCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoise2DCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionsRayTracing>>(
			RE::VTABLE_BSImagespaceShaderReflectionsRayTracing[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionsDebugSpecMask>>(
			RE::VTABLE_BSImagespaceShaderReflectionsDebugSpecMask[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurH>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurH[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurV>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurV[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCameraZ>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZ[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCameraZAndMipsCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZAndMipsCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCompositeSAO>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAO[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCompositeFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeFog[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCompositeSAOFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAOFog[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISMinify>>(
			RE::VTABLE_BSImagespaceShaderISMinify[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISMinifyContrast>>(
			RE::VTABLE_BSImagespaceShaderISMinifyContrast[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAORawAO>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAO[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAORawAONoTemporal>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAONoTemporal[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAORawAOCS>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAOCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSILComposite>>(
			RE::VTABLE_BSImagespaceShaderISSILComposite[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSILRawInd>>(
			RE::VTABLE_BSImagespaceShaderISSILRawInd[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSimpleColor>>(
			RE::VTABLE_BSImagespaceShaderISSimpleColor[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDisplayDepth>>(
			RE::VTABLE_BSImagespaceShaderISDisplayDepth[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSnowSSS>>(
			RE::VTABLE_BSImagespaceShaderISSnowSSS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTemporalAA>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTemporalAA_UI>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_UI[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTemporalAA_Water>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_Water[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISUpsampleDynamicResolution>>(
			RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterBlend>>(
			RE::VTABLE_BSImagespaceShaderISWaterBlend[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISUnderwaterMask>>(
			RE::VTABLE_BSImagespaceShaderISUnderwaterMask[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterFlow>>(
			RE::VTABLE_BSImagespaceShaderWaterFlow[0]);

		stl::write_vfunc<0x2A, BSShaderAccumulator_FinishAccumulatingDispatch>(
			RE::VTABLE_BSShaderAccumulator[0]);

		stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

		stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(
			RE::VTABLE_BSShadowDirectionalLight[0]);
		stl::write_vfunc<0xA, BSShadowFrustumLight_RenderShadowmaps>(
			RE::VTABLE_BSShadowFrustumLight[0]);
		stl::write_vfunc<0xA, BSShadowParabolicLight_RenderShadowmaps>(
			RE::VTABLE_BSShadowParabolicLight[0]);

		stl::detour_thunk<BSBatchRenderer_RenderBatches>(REL::RelocationID(100852, 107642));
		stl::detour_thunk<Main_RenderDepth>(REL::RelocationID(100421, 107139));
		stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));
		stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
		stl::detour_thunk<Main_RenderFirstPersonView>(REL::RelocationID(100411, 107129));
		stl::detour_thunk<Main_RenderPlayerView>(REL::RelocationID(35560, 36559));
		stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));
		stl::detour_thunk<BSShaderAccumulator_RenderBatches>(REL::RelocationID(99963, 106609));
		stl::detour_thunk<BSShaderAccumulator_RenderPersistentPassList>(REL::RelocationID(100840, 107630));
		stl::detour_thunk<BSShaderAccumulator_RenderEffects>(REL::RelocationID(99940, 106585));
		stl::detour_thunk<VolumetricLightingDescriptor_Render>(REL::RelocationID(100306, 107023));
	}

	void OnDataLoaded()
	{
		if (!globals::state->frameAnnotations)
			return;

		auto renderer = globals::game::renderer;

		for (size_t renderTargetIndex = 0;
			 renderTargetIndex < (!REL::Module::IsVR() ? RE::RENDER_TARGETS::kTOTAL : RE::RENDER_TARGETS::kVRTOTAL); ++renderTargetIndex) {
			const auto renderTargetName = magic_enum::enum_name(
				static_cast<RE::RENDER_TARGETS::RENDER_TARGET>(renderTargetIndex));
			if (auto texture = renderer->GetRuntimeData().renderTargets[renderTargetIndex].texture) {
				texture->SetPrivateData(WKPDID_D3DDebugObjectName,
					static_cast<UINT>(renderTargetName.size()), renderTargetName.data());
			}
		}

		for (size_t renderTargetIndex = 0;
			 renderTargetIndex < RE::RENDER_TARGETS_CUBEMAP::kTOTAL;
			 ++renderTargetIndex) {
			const auto renderTargetName = magic_enum::enum_name(
				static_cast<RE::RENDER_TARGETS_CUBEMAP::RENDER_TARGET_CUBEMAP>(renderTargetIndex));
			if (auto texture = renderer->GetRendererData().cubemapRenderTargets[renderTargetIndex].texture) {
				texture->SetPrivateData(WKPDID_D3DDebugObjectName,
					static_cast<UINT>(renderTargetName.size()), renderTargetName.data());
			}
		}

		for (size_t renderTargetIndex = 0;
			 renderTargetIndex < (!REL::Module::IsVR() ? RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL : RE::RENDER_TARGETS_DEPTHSTENCIL::kVRTOTAL);
			 ++renderTargetIndex) {
			const auto renderTargetName = magic_enum::enum_name(
				static_cast<RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL>(
					renderTargetIndex));
			if (auto texture = renderer->GetDepthStencilData().depthStencils[renderTargetIndex].texture) {
				texture->SetPrivateData(WKPDID_D3DDebugObjectName,
					static_cast<UINT>(renderTargetName.size()), renderTargetName.data());
			}
		}
	}
}