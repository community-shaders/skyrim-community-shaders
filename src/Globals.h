#pragma once

struct CloudShadows;
struct DynamicCubemaps;
struct ExtendedMaterials;
struct GrassCollision;
struct GrassLighting;
struct HairSpecular;
struct IBL;
struct LightLimitFix;
struct LODBlending;
struct InteriorSunShadows;
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
struct VolumetricLighting;
struct VR;
struct WaterEffects;
struct WeatherPicker;
struct PerformanceOverlay;
struct WetnessEffects;
struct ExtendedTranslucency;

class ParticleLights;

class State;
class Deferred;
struct TruePBR;
class Menu;
class Streamline;
class Upscaling;
class DX12SwapChain;
class FidelityFX;

namespace SIE
{
	class ShaderCache;
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
		extern CloudShadows* cloudShadows;
		extern DynamicCubemaps* dynamicCubemaps;
		extern ExtendedMaterials* extendedMaterials;
		extern GrassCollision* grassCollision;
		extern GrassLighting* grassLighting;
		extern HairSpecular* hairSpecular;
		extern IBL* ibl;
		extern LightLimitFix* lightLimitFix;
		extern LODBlending* lodBlending;
		extern InteriorSunShadows* interiorSunShadows;
		extern InverseSquareLighting* inverseSquareLighting;
		extern ScreenSpaceGI* screenSpaceGI;
		extern ScreenSpaceShadows* screenSpaceShadows;
		extern Skylighting* skylighting;
		extern TerrainVariation* terrainVariation;
		extern SkySync* skySync;
		extern SubsurfaceScattering* subsurfaceScattering;
		extern TerrainBlending* terrainBlending;
		extern TerrainHelper* terrainHelper;
		extern TerrainShadows* terrainShadows;
		extern VolumetricLighting* volumetricLighting;
		extern VR* vr;
		extern WaterEffects* waterEffects;
		extern WeatherPicker* weatherPicker;
		extern PerformanceOverlay* performanceOverlay;
		extern WetnessEffects* wetnessEffects;
		extern ExtendedTranslucency* extendedTranslucency;

		namespace llf
		{
			extern ParticleLights* particleLights;
		}
	}

	namespace game
	{
		extern RE::BSGraphics::RendererShadowState* shadowState;
		extern RE::BSGraphics::State* graphicsState;
		extern RE::BSGraphics::Renderer* renderer;
		extern RE::BSShaderManager::State* smState;
		extern RE::TES* tes;
		extern bool isVR;
		extern RE::MemoryManager* memoryManager;
		extern RE::INISettingCollection* iniSettingCollection;
		extern RE::INIPrefSettingCollection* iniPrefSettingCollection;
		extern RE::GameSettingCollection* gameSettingCollection;
		extern float* cameraNear;
		extern float* cameraFar;
		extern float* deltaTime;
		extern RE::BSUtilityShader* utilityShader;
		extern RE::Sky* sky;
		extern RE::UI* ui;

		extern RE::BSGraphics::PixelShader** currentPixelShader;
		extern RE::BSGraphics::VertexShader** currentVertexShader;
		extern REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags;

		extern RE::Setting* bEnableLandFade;
		extern RE::Setting* bShadowsOnGrass;
		extern RE::Setting* shadowMaskQuarter;
		extern REL::Relocation<ID3D11Buffer**> perFrame;

		// Cached runtime data pointers for performance
		extern void* cachedRendererRuntimeData;
		extern void* cachedShadowStateRuntimeData;
		extern void* cachedGraphicsStateRuntimeData;
	}

	extern State* state;
	extern Deferred* deferred;
	extern TruePBR* truePBR;
	extern Menu* menu;
	extern SIE::ShaderCache* shaderCache;
	extern Streamline* streamline;
	extern Upscaling* upscaling;
	extern DX12SwapChain* dx12SwapChain;
	extern FidelityFX* fidelityFX;

	void OnInit();
	void ReInit();
	void OnDataLoaded();

	// Cached runtime data accessors for performance
	namespace cached {
		// Get renderer runtime data with caching
		inline auto& GetRendererRuntimeData() {
			if (game::cachedRendererRuntimeData) {
				if (game::isVR) {
					return *static_cast<decltype(game::renderer->GetVRRuntimeData())*>(game::cachedRendererRuntimeData);
				} else {
					return *static_cast<decltype(game::renderer->GetRuntimeData())*>(game::cachedRendererRuntimeData);
				}
			}
			return game::isVR ? game::renderer->GetVRRuntimeData() : game::renderer->GetRuntimeData();
		}

		// Get shadow state runtime data with caching
		inline auto& GetShadowStateRuntimeData() {
			if (game::cachedShadowStateRuntimeData) {
				if (game::isVR) {
					return *static_cast<decltype(game::shadowState->GetVRRuntimeData())*>(game::cachedShadowStateRuntimeData);
				} else {
					return *static_cast<decltype(game::shadowState->GetRuntimeData())*>(game::cachedShadowStateRuntimeData);
				}
			}
			return game::isVR ? game::shadowState->GetVRRuntimeData() : game::shadowState->GetRuntimeData();
		}

		// Get graphics state runtime data with caching
		inline auto& GetGraphicsStateRuntimeData() {
			if (game::cachedGraphicsStateRuntimeData) {
				if (game::isVR) {
					return *static_cast<decltype(game::graphicsState->GetVRRuntimeData())*>(game::cachedGraphicsStateRuntimeData);
				} else {
					return *static_cast<decltype(game::graphicsState->GetRuntimeData())*>(game::cachedGraphicsStateRuntimeData);
				}
			}
			return game::isVR ? game::graphicsState->GetVRRuntimeData() : game::graphicsState->GetRuntimeData();
		}
	}
}