#include "Globals.h"

#include "Utils/Game.h"

#include "Deferred.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Streamline.h"
#include "Upscaling.h"

#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/LightLimitFix.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainShadows.h"
#include "Features/VolumetricLighting.h"
#include "Features/WaterEffects.h"
#include "Features/WetnessEffects.h"

#include "Features/LightLimitFix/ParticleLights.h"

#include "TruePBR.h"

namespace globals
{
	namespace d3d
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain* swapchain = nullptr;
	}

	namespace features
	{
		CloudShadows* cloudShadows = nullptr;
		DynamicCubemaps* dynamicCubemaps = nullptr;
		ExtendedMaterials* extendedMaterials = nullptr;
		GrassCollision* grassCollision = nullptr;
		GrassLighting* grassLighting = nullptr;
		LightLimitFix* lightLimitFix = nullptr;
		ScreenSpaceGI* screenSpaceGI = nullptr;
		ScreenSpaceShadows* screenSpaceShadows = nullptr;
		Skylighting* skylighting = nullptr;
		SubsurfaceScattering* subsurfaceScattering = nullptr;
		TerrainBlending* terrainBlending = nullptr;
		TerrainShadows* terrainShadows = nullptr;
		VolumetricLighting* volumetricLighting = nullptr;
		WaterEffects* waterEffects = nullptr;
		WetnessEffects* wetnessEffects = nullptr;

		namespace llf
		{
			ParticleLights* particleLights = nullptr;
		}
	}

	namespace game
	{
		RE::BSGraphics::RendererShadowState* shadowState = nullptr;
		RE::BSGraphics::State* graphicsState = nullptr;
		RE::BSGraphics::Renderer* renderer = nullptr;
		RE::BSShaderManager::State* smState = nullptr;
		RE::TES* tes = nullptr;
		bool isVR = false;
		RE::MemoryManager* memoryManager = nullptr;
		RE::INISettingCollection* iniSettingCollection = nullptr;
		RE::INIPrefSettingCollection* iniPrefSettingCollection = nullptr;
		RE::GameSettingCollection* gameSettingCollection = nullptr;
		float* cameraNear = nullptr;
		float* cameraFar = nullptr;
		RE::BSUtilityShader* utilityShader = nullptr;
		RE::Sky* sky = nullptr;
		RE::UI* ui = nullptr;

		RE::BSGraphics::PixelShader** currentPixelShader = nullptr;
		RE::BSGraphics::VertexShader** currentVertexShader = nullptr;
		stl::enumeration<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags = nullptr;

		RE::Setting* bEnableLandFade = nullptr;
		RE::Setting* bShadowsOnGrass = nullptr;
		RE::Setting* shadowMaskQuarter = nullptr;
	}

	State* state = nullptr;
	Deferred* deferred = nullptr;
	TruePBR* truePBR = nullptr;
	Menu* menu = nullptr;
	SIE::ShaderCache* shaderCache = nullptr;
	Streamline* streamline = nullptr;
	Upscaling* upscaling = nullptr;

	void ReInit()
	{
		{
			using namespace game;

			shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			graphicsState = RE::BSGraphics::State::GetSingleton();
			renderer = RE::BSGraphics::Renderer::GetSingleton();
			smState = &RE::BSShaderManager::State::GetSingleton();
			isVR = REL::Module::IsVR();
			memoryManager = RE::MemoryManager::GetSingleton();
			iniSettingCollection = RE::INISettingCollection::GetSingleton();
			iniPrefSettingCollection = RE::INIPrefSettingCollection::GetSingleton();
			gameSettingCollection = RE::GameSettingCollection::GetSingleton();
			cameraNear = (float*)(REL::RelocationID(517032, 403540).address() + 0x40);
			cameraFar = (float*)(REL::RelocationID(517032, 403540).address() + 0x44);

			currentPixelShader = GET_INSTANCE_MEMBER_PTR(currentPixelShader, shadowState);
			currentVertexShader = GET_INSTANCE_MEMBER_PTR(currentVertexShader, shadowState);
			stateUpdateFlags = GET_INSTANCE_MEMBER_PTR(stateUpdateFlags, shadowState);

			ui = RE::UI::GetSingleton();
		}

		d3d::device = reinterpret_cast<ID3D11Device*>(game::renderer->GetRuntimeData().forwarder);
		d3d::context = reinterpret_cast<ID3D11DeviceContext*>(game::renderer->GetRuntimeData().context);
		d3d::swapchain = reinterpret_cast<IDXGISwapChain*>(game::renderer->GetRuntimeData().renderWindows->swapChain);

		state = State::GetSingleton();
		menu = Menu::GetSingleton();
		shaderCache = &SIE::ShaderCache::Instance();
		deferred = Deferred::GetSingleton();
		truePBR = TruePBR::GetSingleton();
		streamline = Streamline::GetSingleton();
		upscaling = Upscaling::GetSingleton();

		features::cloudShadows = CloudShadows::GetSingleton();
		features::dynamicCubemaps = DynamicCubemaps::GetSingleton();
		features::extendedMaterials = ExtendedMaterials::GetSingleton();
		features::grassCollision = GrassCollision::GetSingleton();
		features::grassLighting = GrassLighting::GetSingleton();
		features::lightLimitFix = LightLimitFix::GetSingleton();
		features::screenSpaceGI = ScreenSpaceGI::GetSingleton();
		features::screenSpaceShadows = ScreenSpaceShadows::GetSingleton();
		features::skylighting = Skylighting::GetSingleton();
		features::subsurfaceScattering = SubsurfaceScattering::GetSingleton();
		features::terrainBlending = TerrainBlending::GetSingleton();
		features::terrainShadows = TerrainShadows::GetSingleton();
		features::volumetricLighting = VolumetricLighting::GetSingleton();
		features::waterEffects = WaterEffects::GetSingleton();
		features::wetnessEffects = WetnessEffects::GetSingleton();

		features::llf::particleLights = ParticleLights::GetSingleton();
	}

	void OnDataLoaded()
	{
		using namespace game;
		tes = RE::TES::GetSingleton();
		sky = RE::Sky::GetSingleton();
		utilityShader = RE::BSUtilityShader::GetSingleton();

		bEnableLandFade = iniSettingCollection->GetSetting("bEnableLandFade:Display");

		bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
		shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
	}
}
