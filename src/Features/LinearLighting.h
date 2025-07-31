#pragma once

struct LinearLighting : Feature
{
	static LinearLighting* GetSingleton()
	{
		static LinearLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Linear Lighting"; }
	virtual inline std::string GetShortName() override { return "LinearLighting"; }
	virtual std::string_view GetCategory() const override { return "Lighting"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Linear Lighting does internal color space conversion to improve lighting calculation accuracy.",
			{
				"Customizable gamma correction",
				"Corrects lighting calculations",
				"Makes PBR really work"
			}
		};
	}

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	struct Settings
	{
		uint enableLinearLighting = true;
		uint enableGammaCorrection = true;
		float lightGamma = 1.8f;
		float colorGamma = 2.2f;
		float ambientGamma = 1.8f;
		float fogGamma = 2.2f;
		float effectGamma = 1.8f;
		float effectAlphaGamma = 1.8f;
		float skyGamma = 1.8f;
		float waterGamma = 1.8f;
		float vlGamma = 1.8f;

		// Lighting multipliers
		float vanillaDiffuseMult = 0.66f;
		float vanillaSpecularMult = 0.66f;

		// Effect multipliers
		float membraneEffectMult = 1.0f;
		float bloodEffectMult = 1.0f;
		float projectedEffectMult = 1.0f;
		float deferredEffectMult = 1.0f;
		float otherEffectMult = 1.0f;
	} settings;

	struct alignas(16) PerFrameData
	{
		uint enableLinearLighting;
		uint enableGammaCorrection;
		float dirLightMult;
		float lightGamma;
		float colorGamma;
		float ambientGamma;
		float fogGamma;
		float effectGamma;
		float effectAlphaGamma;
		float skyGamma;
		float waterGamma;
		float vlGamma;
		float vanillaDiffuseMult;
		float vanillaSpecularMult;
		float membraneEffectMult;
		float bloodEffectMult;
		float projectedEffectMult;
		float deferredEffectMult;
		float otherEffectMult;
		float pad;
	};

	uint tempDisable = false;
	float dirLightMult = 1.0f;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void PostPostLoad() override;
	virtual void Prepass() override;

	PerFrameData GetCommonBufferData();

	// Event handler
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
		{
			// Disable linear lighting when entering the loading screen
			if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
				if (a_event->opening)
					globals::features::linearLighting.tempDisable = true;
				else
					globals::features::linearLighting.tempDisable = false;
			}

			return RE::BSEventNotifyControl::kContinue;
		}

		static bool Register()
		{
			static MenuOpenCloseEventHandler singleton;
			auto ui = globals::game::ui;

			if (!ui) {
				logger::error("UI event source not found");
				return false;
			}

			ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};
