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
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->DrawSettings();
}

void HDRDisplay::SetupResources()
{
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->SetupResources();
}

void HDRDisplay::ClearShaderCache()
{
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->ClearShaderCache();
}

void HDRDisplay::ApplyHDR()
{
	auto* hdrSingleton = HDR::GetSingleton();
	if (hdrSingleton)
		hdrSingleton->ApplyHDR();
}