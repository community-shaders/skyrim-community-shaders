#include "ENBPostProcessing.h"

#include "ENBPostProcessing/EffectManager.h"
#include "ENBPostProcessing/MenuManager.h"
#include "ENBPostProcessing/SettingManager.h"

void ENBPostProcessing::SaveSettings(json&)
{
}

void ENBPostProcessing::LoadSettings(json&)
{
}

void ENBPostProcessing::RestoreDefaultSettings()
{
}

void ENBPostProcessing::DrawSettings()
{
	MenuManager::GetSingleton().RenderImGui();
}

void ENBPostProcessing::SetupResources()
{
	// Create shared texture resources
	TextureManager::GetSingleton().Initialize();

	// Then initialize the effects system
	EffectManager::GetSingleton().Initialize();

	// Load registered settings
	SettingManager::GetSingleton().Load();
}

void ENBPostProcessing::Reset()
{
	// Reset effect state if needed
}

struct Main_HDRTonemapBlendCinematic_Render
{
	static void thunk(RE::ImageSpaceManager*, RE::ImageSpaceEffect*, uint32_t, uint32_t, RE::ImageSpaceShaderParam*)
	{
		auto& effectManager = EffectManager::GetSingleton();

		effectManager.UpdateCommonData();

		const auto& commonData = effectManager.GetCommonData();
		auto& settingManager = SettingManager::GetSingleton();
		settingManager.SetTimeOfDayData(commonData.timeOfDay1, commonData.timeOfDay2, commonData.eInteriorFactor);
		settingManager.SetWeatherBlendFactors(
			static_cast<uint32_t>(commonData.weather[0]),
			static_cast<uint32_t>(commonData.weather[1]),
			commonData.weather[2]);

		effectManager.ExecuteEffects();

		//func(a1, a2, a3, a4, a5);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

void ENBPostProcessing::PostPostLoad()
{
	stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x1EA, 0x178));
	if (REL::Module::IsSE())
		stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x230, 0x178));
}
