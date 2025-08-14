#include "PCH.h"
#include "ENBPostProcessing.h"
#include "State.h"

void ENBPostProcessing::SaveSettings(json& o_json)
{
	o_json["Enabled"] = settings.Enabled;
	o_json["EffectPath"] = settings.EffectPath;
}

void ENBPostProcessing::LoadSettings(json& o_json)
{
	if (o_json["Enabled"].is_boolean())
		settings.Enabled = o_json["Enabled"];
	
	if (o_json["EffectPath"].is_string())
		settings.EffectPath = o_json["EffectPath"];
}

void ENBPostProcessing::RestoreDefaultSettings()
{
	settings.Enabled = false;
	settings.EffectPath = "";
}

void ENBPostProcessing::DrawSettings()
{	
	effect11.RenderImGui();
}

void ENBPostProcessing::SetupResources()
{
	effect11.Initialize();
	effect11.LoadFXFile("enbseries/enbeffect.fx");
}

void ENBPostProcessing::Reset()
{
	// Reset effect state if needed
}