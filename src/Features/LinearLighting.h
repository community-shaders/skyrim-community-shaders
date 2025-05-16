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

    struct Settings
    {
        uint enableLinearLighting = true;
        uint enableGammaCorrection = true;
        uint pad[2];
    } settings;

    virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

    virtual void PostPostLoad() override;

    // Event handler
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
		{
			// Disable linear lighting when entering the loading screen
			if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
				bool original = GetSingleton()->settings.enableLinearLighting;
				if (a_event->opening)
					GetSingleton()->settings.enableLinearLighting = false;
                else
                    GetSingleton()->settings.enableLinearLighting = true && original;
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
