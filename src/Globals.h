#pragma once

#include <atomic>

struct CloudShadows;
struct DynamicCubemaps;
struct VolumetricShadows;
struct ExtendedMaterials;
struct GrassCollision;
struct GrassLighting;
struct HairSpecular;
struct IBL;
struct LightLimitFix;
struct LinearLighting;
struct LODBlending;
struct InteriorSun;
struct InverseSquareLighting;
struct ScreenSpaceGI;
struct ScreenSpaceShadows;
struct Skylighting;
struct TerrainVariation;
struct SkySync;
struct SubsurfaceScattering;
struct TerrainBlending;
struct TerrainHelper;
struct TerrainShadows;
struct UnifiedWater;
struct VolumetricLighting;
struct WaterEffects;
struct PerformanceOverlay;
struct WetnessEffects;
struct ExtendedTranslucency;
struct Upscaling;
class Profiler;
struct CSEditor;
struct ExponentialHeightFog;
struct HDRDisplay;
struct ScreenshotFeature;
struct Skin;

class State;
class Deferred;
struct TruePBR;
class RenderDoc;
class Menu;

namespace SIE
{
	class ShaderCache;
	class ShaderFileDependencyTracker;
}

namespace globals
{
	namespace d3d
	{
		extern ID3D11Device* device;
		extern ID3D11DeviceContext* context;
		extern IDXGISwapChain* swapChain;
	}

	namespace features
	{
		extern CloudShadows cloudShadows;
		extern DynamicCubemaps dynamicCubemaps;
		extern VolumetricShadows volumetricShadows;
		extern ExtendedMaterials extendedMaterials;
		extern GrassCollision grassCollision;
		extern GrassLighting grassLighting;
		extern HairSpecular hairSpecular;
		extern IBL ibl;
		extern LightLimitFix lightLimitFix;
		extern LinearLighting linearLighting;
		extern LODBlending lodBlending;
		extern InteriorSun interiorSun;
		extern InverseSquareLighting inverseSquareLighting;
		extern ScreenSpaceGI screenSpaceGI;
		extern ScreenSpaceShadows screenSpaceShadows;
		extern Skylighting skylighting;
		extern TerrainVariation terrainVariation;
		extern SkySync skySync;
		extern SubsurfaceScattering subsurfaceScattering;
		extern TerrainBlending terrainBlending;
		extern TerrainHelper terrainHelper;
		extern TerrainShadows terrainShadows;
		extern UnifiedWater unifiedWater;
		extern VolumetricLighting volumetricLighting;
		extern WaterEffects waterEffects;
		extern PerformanceOverlay performanceOverlay;
		extern WetnessEffects wetnessEffects;
		extern ExtendedTranslucency extendedTranslucency;
		extern Upscaling upscaling;
		extern HDRDisplay hdrDisplay;
		extern RenderDoc renderDoc;
		extern ScreenshotFeature screenshotFeature;
		extern CSEditor csEditor;
		extern ExponentialHeightFog exponentialHeightFog;
		extern TruePBR truePBR;
		extern Skin skin;

	}

	struct FrameBuffer
	{
		Matrix CameraView;
		Matrix CameraProj;
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraProjUnjittered;
		Matrix CameraProjUnjitteredInverse;
		Matrix CameraViewInverse;
		Matrix CameraViewProjInverse;
		Matrix CameraProjInverse;
		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	struct FrameBufferCache
	{
		FrameBuffer data;

		const Matrix& GetCameraView() const { return data.CameraView; }
		const Matrix& GetCameraProj() const { return data.CameraProj; }
		const Matrix& GetCameraViewProj() const { return data.CameraViewProj; }
		const Matrix& GetCameraViewProjUnjittered() const { return data.CameraViewProjUnjittered; }
		const Matrix& GetCameraPreviousViewProjUnjittered() const { return data.CameraPreviousViewProjUnjittered; }
		const Matrix& GetCameraProjUnjittered() const { return data.CameraProjUnjittered; }
		const Matrix& GetCameraProjUnjitteredInverse() const { return data.CameraProjUnjitteredInverse; }
		const Matrix& GetCameraViewInverse() const { return data.CameraViewInverse; }
		const Matrix& GetCameraViewProjInverse() const { return data.CameraViewProjInverse; }
		const Matrix& GetCameraProjInverse() const { return data.CameraProjInverse; }
		const float4& GetCameraPosAdjust() const { return data.CameraPosAdjust; }
		const float4& GetCameraPreviousPosAdjust() const { return data.CameraPreviousPosAdjust; }
		const float4& GetFrameParams() const { return data.FrameParams; }
		const float4& GetDynamicResolutionParams1() const { return data.DynamicResolutionParams1; }
		const float4& GetDynamicResolutionParams2() const { return data.DynamicResolutionParams2; }
	};

	namespace game
	{
		extern RE::BSGraphics::RendererShadowState* shadowState;
		extern RE::BSGraphics::State* graphicsState;
		extern RE::BSGraphics::Renderer* renderer;
		extern RE::BSShaderManager::State* smState;
		extern RE::TES* tes;
		extern RE::MemoryManager* memoryManager;
		extern RE::INISettingCollection* iniSettingCollection;
		extern RE::INIPrefSettingCollection* iniPrefSettingCollection;
		extern RE::GameSettingCollection* gameSettingCollection;
		extern float* cameraNear;
		extern float* cameraFar;
		extern float* deltaTime;
		extern RE::BSUtilityShader* utilityShader;
		extern RE::PlayerCharacter* player;
		extern RE::Sky* sky;
		extern RE::UI* ui;
		extern RE::Calendar* calendar;
		extern std::atomic<bool> quitGame;

		extern RE::BSGraphics::PixelShader** currentPixelShader;
		extern RE::BSGraphics::VertexShader** currentVertexShader;
		extern REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags;

		extern RE::Setting* bEnableLandFade;
		extern RE::Setting* bShadowsOnGrass;
		extern RE::Setting* shadowMaskQuarter;
		extern REL::Relocation<ID3D11Buffer**> perFrame;
		extern REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;

		extern D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer;
		extern FrameBufferCache frameBufferCached;
	}

	namespace rtti
	{
		extern REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSWaterShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiSourceTextureRTTI;
	}

	extern State* state;
	extern Deferred* deferred;
	extern Menu* menu;
	extern SIE::ShaderCache* shaderCache;
	extern Profiler* profiler;

	void OnInit();
	void ReInit();
	void OnDataLoaded();
	void OnGameWindowClose();
	void InstallD3DHooks(ID3D11DeviceContext* a_context);
}