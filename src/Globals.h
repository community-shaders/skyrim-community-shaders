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
struct VolumetricLighting;
struct VR;
struct WaterEffects;
struct WeatherPicker;
struct PerformanceOverlay;
struct WetnessEffects;
struct ExtendedTranslucency;
struct PostProcessing;

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
		extern CloudShadows cloudShadows;
		extern DynamicCubemaps dynamicCubemaps;
		extern ExtendedMaterials extendedMaterials;
		extern GrassCollision grassCollision;
		extern GrassLighting grassLighting;
		extern HairSpecular hairSpecular;
		extern IBL ibl;
		extern LightLimitFix lightLimitFix;
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
		extern VolumetricLighting volumetricLighting;
		extern VR vr;
		extern WaterEffects waterEffects;
		extern WeatherPicker weatherPicker;
		extern PerformanceOverlay performanceOverlay;
		extern WetnessEffects wetnessEffects;
		extern ExtendedTranslucency extendedTranslucency;
		extern PostProcessing postProcessing;

		namespace llf
		{
			extern ParticleLights particleLights;
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
		extern REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;
	}

	namespace rtti
	{
		extern REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
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
}