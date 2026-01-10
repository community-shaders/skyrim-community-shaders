#include "HDRDisplay.h"

#include "Globals.h"
#include "State.h"

void HDRDisplay::RestoreDefaultSettings()
{
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->RestoreDefaultSettings();
}

void HDRDisplay::LoadSettings(json& o_json)
{
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->LoadSettings(o_json);
}

void HDRDisplay::SaveSettings(json& o_json)
{
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->SaveSettings(o_json);
}

void HDRDisplay::DrawSettings()
{
	if (hdr)
		hdr->DrawSettings();
}

void HDRDisplay::SetupResources()
{
	hdr = HDR::GetSingleton();
	if (hdr)
		hdr->SetupResources();
}

void HDRDisplay::ClearShaderCache()
{
	if (hdr)
		hdr->ClearShaderCache();
}

bool HDRDisplay::IsHDREnabled() const
{
	return hdr && hdr->settings.enableHDR;
}

void HDRDisplay::ApplyHDR()
{
	if (hdr)
		hdr->ApplyHDR();
}
