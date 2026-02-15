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

void HDRDisplay::DataLoaded()
{
	// Use Skyrim's built-in ini setting to upgrade all HDR render targets to 16-bit float format.
	auto setting = RE::GetINISetting("bUse64bitsHDRRenderTarget:Display");
	if (setting) {
		setting->data.b = true;
		logger::info("[HDR Display] Enabled bUse64bitsHDRRenderTarget - all required render targets will use R16G16B16A16_FLOAT");
	} else {
		logger::warn("[HDR Display] bUse64bitsHDRRenderTarget ini setting not found");
	}
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