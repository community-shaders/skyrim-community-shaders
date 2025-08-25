#pragma once

#include <functional>
#include <string>

class HomePageRenderer
{
public:
	static void RenderHomePage();

private:
	static void RenderWelcomeSection();
	static void RenderQuickLinksSection();
	static void RenderFAQSection();
	static void RenderFirstTimeSetupDialog();
	
	// First-time setup management
	static bool ShouldShowFirstTimeSetup();
	static void MarkFirstTimeSetupComplete();
	
	// State
	static bool isFirstTimeSetupShown;
};
