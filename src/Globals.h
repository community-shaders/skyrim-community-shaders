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

	/** @brief GPU constant buffer layout matching Skyrim's per-frame camera data. */
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

	/** @brief Cached snapshot of per-frame camera data, captured between Map/Unmap of the per-frame constant buffer. */
	struct FrameBufferCache
	{
		FrameBuffer data;

		/** Gets the camera view matrix. */
		const Matrix& GetCameraView() const { return data.CameraView; }
		/** Gets the camera projection matrix. */
		const Matrix& GetCameraProj() const { return data.CameraProj; }
		/** Gets the combined camera view-projection matrix. */
		const Matrix& GetCameraViewProj() const { return data.CameraViewProj; }
		/** Gets the unjittered camera view-projection matrix. */
		const Matrix& GetCameraViewProjUnjittered() const { return data.CameraViewProjUnjittered; }
		/** Gets the previous frame's unjittered view-projection matrix. */
		const Matrix& GetCameraPreviousViewProjUnjittered() const { return data.CameraPreviousViewProjUnjittered; }
		/** Gets the unjittered camera projection matrix. */
		const Matrix& GetCameraProjUnjittered() const { return data.CameraProjUnjittered; }
		/** Gets the inverse of the unjittered camera projection matrix. */
		const Matrix& GetCameraProjUnjitteredInverse() const { return data.CameraProjUnjitteredInverse; }
		/** Gets the inverse camera view matrix. */
		const Matrix& GetCameraViewInverse() const { return data.CameraViewInverse; }
		/** Gets the inverse camera view-projection matrix. */
		const Matrix& GetCameraViewProjInverse() const { return data.CameraViewProjInverse; }
		/** Gets the inverse camera projection matrix. */
		const Matrix& GetCameraProjInverse() const { return data.CameraProjInverse; }
		/** Gets the camera position adjustment vector. */
		const float4& GetCameraPosAdjust() const { return data.CameraPosAdjust; }
		/** Gets the previous frame's camera position adjustment vector. */
		const float4& GetCameraPreviousPosAdjust() const { return data.CameraPreviousPosAdjust; }
		/** Gets the frame parameters (timer, frame count, etc.). */
		const float4& GetFrameParams() const { return data.FrameParams; }
		/** Gets the first set of dynamic resolution parameters. */
		const float4& GetDynamicResolutionParams1() const { return data.DynamicResolutionParams1; }
		/** Gets the second set of dynamic resolution parameters. */
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
		extern RE::ImageSpaceManager* imageSpaceManager;
		extern bool* bEnableVolumetricLighting;
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

	/** @brief Initializes core singletons (ShaderCache, State, Menu, Deferred). Called once at plugin load. */
	void OnInit();
	/** @brief Resolves runtime game pointers, RTTI relocations, and D3D device references. Called when the renderer is ready. */
	void ReInit();
	/** @brief Caches late-binding game singletons (player, sky, INI settings) after Skyrim's data files are loaded. */
	void OnDataLoaded();
	/** @brief Signals shader compilation to stop when the game window is closing. */
	void OnGameWindowClose();
	/**
	 * @brief Installs Detours hooks on the device context's Map/Unmap vtable slots to capture per-frame constant buffer data.
	 * @param a_context The D3D11 device context to hook.
	 */
	void InstallD3DHooks(ID3D11DeviceContext* a_context);
}