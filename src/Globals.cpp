#include "Globals.h"

#include "Utils/Game.h"

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "FidelityFX.h"
#include "Menu.h"
#include "XeSS.h"
#include "ShaderCache.h"
#include "State.h"
#include "Streamline.h"
#include "Upscaling.h"

#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/InteriorSunShadows.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/PerformanceOverlay.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"
#include "Features/WaterEffects.h"
#include "Features/WeatherPicker.h"
#include "Features/WetnessEffects.h"

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
		CloudShadows cloudShadows{};
		DynamicCubemaps dynamicCubemaps{};
		ExtendedMaterials extendedMaterials{};
		GrassCollision grassCollision{};
		GrassLighting grassLighting{};
		IBL ibl{};
		LightLimitFix lightLimitFix{};
		LODBlending lodBlending{};
		HairSpecular hairSpecular{};
		InteriorSunShadows interiorSunShadows{};
		InverseSquareLighting inverseSquareLighting{};
		ScreenSpaceGI screenSpaceGI{};
		ScreenSpaceShadows screenSpaceShadows{};
		Skylighting skylighting{};
		TerrainVariation terrainVariation{};
		SkySync skySync{};
		SubsurfaceScattering subsurfaceScattering{};
		TerrainBlending terrainBlending{};
		TerrainHelper terrainHelper{};
		TerrainShadows terrainShadows{};
		VolumetricLighting volumetricLighting{};
		VR vr{};
		WaterEffects waterEffects{};
		WeatherPicker weatherPicker{};
		PerformanceOverlay performanceOverlay{};
		WetnessEffects wetnessEffects{};
		ExtendedTranslucency extendedTranslucency{};

		namespace llf
		{
			ParticleLights particleLights{};
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
		REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags = nullptr;

		RE::Setting* bEnableLandFade = nullptr;
		RE::Setting* bShadowsOnGrass = nullptr;
		RE::Setting* shadowMaskQuarter = nullptr;

		REL::Relocation<ID3D11Buffer**> perFrame;
		REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;
	}

	namespace rtti
	{
		REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
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
	XeSS* xess = nullptr;

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
		xess = XeSS::GetSingleton();
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

			currentAccumulator = { REL::RelocationID(527650, 414600) };
		}

		{
			using namespace rtti;
			NiIntegerExtraDataRTTI = { RE::NiIntegerExtraData::Ni_RTTI };
			BSLightingShaderPropertyRTTI = { RE::BSLightingShaderProperty::Ni_RTTI };
			BSEffectShaderPropertyRTTI = { RE::BSEffectShaderProperty::Ni_RTTI };
			NiParticleSystemRTTI = { RE::NiParticleSystem::Ni_RTTI };
			NiBillboardNodeRTTI = { RE::NiBillboardNode::Ni_RTTI };
			NiAlphaPropertyRTTI = { RE::NiAlphaProperty::Ni_RTTI };
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
