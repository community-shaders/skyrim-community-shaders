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

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

    struct alignas(16) Settings
    {
        uint enableLinearLighting = true;
        uint enableGammaCorrection = true;
		uint preserveLightLuminance = false;
        float lightGamma = 1.8f;
		float colorGamma = 2.2f;
		float ambientGamma = 1.8f;
		float pad[2];
    } settings;

	uint tempDisable = false;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void PostPostLoad() override;

	Settings GetCommonBufferData();

	// Event handler
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
		{
			// Disable linear lighting when entering the loading screen
			if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
				if (a_event->opening)
					GetSingleton()->tempDisable = true;
				else
					GetSingleton()->tempDisable = false;
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
