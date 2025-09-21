#pragma once

#include <string>

class HomePageRenderer
{
public:
	// Constants
	static constexpr const char* DISCORD_URL = "https://discord.com/invite/nkrQybAsyy";
	static constexpr float TITLE_FONT_SCALE = 2.0f;
	static constexpr float QUICK_LINKS_BUTTON_WIDTH = 180.0f;
	static constexpr float LOGO_WATERMARK_HEIGHT = 260.0f;

	static void RenderHomePage();

	// First-time setup management
	static bool ShouldShowFirstTimeSetup();
	static void RenderFirstTimeSetupDialog();

private:
	static void RenderWelcomeSection();
	static void RenderQuickLinksSection();
	static void RenderFAQSection();

	static void MarkFirstTimeSetupComplete();

	// State
	static bool isFirstTimeSetupShown;
};
