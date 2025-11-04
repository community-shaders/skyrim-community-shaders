#pragma once

#include <string>

class HomePageRenderer
{
public:
	// Constants
	static constexpr const char* DISCORD_URL = "https://discord.com/invite/nkrQybAsyy";
	static constexpr float TITLE_FONT_SCALE = 2.0f;
	static constexpr float QUICK_LINKS_BUTTON_WIDTH = 180.0f;
	static constexpr float LOGO_WATERMARK_HEIGHT = 200.0f;

	// Discord banner scaling constants
	static constexpr float DISCORD_BANNER_TARGET_WIDTH_RATIO = 0.85f;  // 25% of window width
	static constexpr float DISCORD_BANNER_MIN_WIDTH = 150.0f;
	static constexpr float DISCORD_BANNER_MAX_WIDTH = 1200.0f;
	static constexpr float DISCORD_BANNER_PADDING_MARGIN = 40.0f;

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
