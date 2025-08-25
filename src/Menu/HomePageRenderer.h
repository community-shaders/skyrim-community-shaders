#pragma once

#include <functional>
#include <string>

class HomePageRenderer
{
public:
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
