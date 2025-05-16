#include "Globals.h"

#include "Utils/Game.h"

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "FidelityFX.h"
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
#include "Features/HairSpecular.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/TerrainShadows.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"
#include "Features/WaterEffects.h"
#include "Features/WetnessEffects.h"
#include "Features/PostProcessing.h"

#include "Features/LightLimitFix/ParticleLights.h"

#include "TruePBR.h"

namespace globals
{
	namespace d3d
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain* swapChain = nullptr;
	}

	namespace features
	{
		CloudShadows* cloudShadows = nullptr;
		DynamicCubemaps* dynamicCubemaps = nullptr;
		ExtendedMaterials* extendedMaterials = nullptr;
		GrassCollision* grassCollision = nullptr;
		GrassLighting* grassLighting = nullptr;
		LightLimitFix* lightLimitFix = nullptr;
		LODBlending* lodBlending = nullptr;
		HairSpecular* hairSpecular = nullptr;
		InverseSquareLighting* inverseSquareLighting = nullptr;
		ScreenSpaceGI* screenSpaceGI = nullptr;
		ScreenSpaceShadows* screenSpaceShadows = nullptr;
		Skylighting* skylighting = nullptr;
		SkySync* skySync = nullptr;
		SubsurfaceScattering* subsurfaceScattering = nullptr;
		TerrainBlending* terrainBlending = nullptr;
		TerrainHelper* terrainHelper = nullptr;
		TerrainShadows* terrainShadows = nullptr;
		VolumetricLighting* volumetricLighting = nullptr;
		VR* vr = nullptr;
		WaterEffects* waterEffects = nullptr;
		WetnessEffects* wetnessEffects = nullptr;
		PostProcessing* postProcessing = nullptr;

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
		float* deltaTime = nullptr;
		RE::BSUtilityShader* utilityShader = nullptr;
		RE::Sky* sky = nullptr;
		RE::UI* ui = nullptr;

		RE::BSGraphics::PixelShader** currentPixelShader = nullptr;
		RE::BSGraphics::VertexShader** currentVertexShader = nullptr;
		stl::enumeration<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags = nullptr;

		RE::Setting* bEnableLandFade = nullptr;
		RE::Setting* bShadowsOnGrass = nullptr;
		RE::Setting* shadowMaskQuarter = nullptr;

		REL::Relocation<ID3D11Buffer**> perFrame;
	}

	State* state = nullptr;
	Deferred* deferred = nullptr;
	TruePBR* truePBR = nullptr;
	Menu* menu = nullptr;
	SIE::ShaderCache* shaderCache = nullptr;
	Streamline* streamline = nullptr;
	Upscaling* upscaling = nullptr;
	DX12SwapChain* dx12SwapChain = nullptr;
	FidelityFX* fidelityFX = nullptr;

	void OnInit()
	{
		shaderCache = &SIE::ShaderCache::Instance();
		state = State::GetSingleton();
		menu = Menu::GetSingleton();
		deferred = Deferred::GetSingleton();
		truePBR = TruePBR::GetSingleton();
		streamline = Streamline::GetSingleton();
		upscaling = Upscaling::GetSingleton();
		dx12SwapChain = DX12SwapChain::GetSingleton();
		fidelityFX = FidelityFX::GetSingleton();

		features::cloudShadows = CloudShadows::GetSingleton();
		features::dynamicCubemaps = DynamicCubemaps::GetSingleton();
		features::extendedMaterials = ExtendedMaterials::GetSingleton();
		features::grassCollision = GrassCollision::GetSingleton();
		features::grassLighting = GrassLighting::GetSingleton();
		features::hairSpecular = HairSpecular::GetSingleton();
		features::lightLimitFix = LightLimitFix::GetSingleton();
		features::lodBlending = LODBlending::GetSingleton();
		features::inverseSquareLighting = InverseSquareLighting::GetSingleton();
		features::screenSpaceGI = ScreenSpaceGI::GetSingleton();
		features::screenSpaceShadows = ScreenSpaceShadows::GetSingleton();
		features::skylighting = Skylighting::GetSingleton();
		features::skySync = SkySync::GetSingleton();
		features::subsurfaceScattering = SubsurfaceScattering::GetSingleton();
		features::terrainBlending = TerrainBlending::GetSingleton();
		features::terrainHelper = TerrainHelper::GetSingleton();
		features::terrainShadows = TerrainShadows::GetSingleton();
		features::volumetricLighting = VolumetricLighting::GetSingleton();
		features::vr = VR::GetSingleton();
		features::waterEffects = WaterEffects::GetSingleton();
		features::wetnessEffects = WetnessEffects::GetSingleton();
		features::postProcessing = PostProcessing::GetSingleton();

		features::llf::particleLights = ParticleLights::GetSingleton();
	}

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
			deltaTime = (float*)REL::RelocationID(523660, 410199).address();

			currentPixelShader = GET_INSTANCE_MEMBER_PTR(currentPixelShader, shadowState);
			currentVertexShader = GET_INSTANCE_MEMBER_PTR(currentVertexShader, shadowState);
			stateUpdateFlags = GET_INSTANCE_MEMBER_PTR(stateUpdateFlags, shadowState);

			ui = RE::UI::GetSingleton();
			perFrame = { REL::RelocationID(524768, 411384) };
		}

		d3d::device = reinterpret_cast<ID3D11Device*>(game::renderer->GetRuntimeData().forwarder);
		d3d::context = reinterpret_cast<ID3D11DeviceContext*>(game::renderer->GetRuntimeData().context);
		d3d::swapChain = reinterpret_cast<IDXGISwapChain*>(game::renderer->GetRuntimeData().renderWindows->swapChain);
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
