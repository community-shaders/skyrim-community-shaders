#include "EngineFix.h"

#include "EngineFixes/EffectShaderNoDecalsFix.h"
#include "EngineFixes/LandscapeSeamsFix.h"
#include "EngineFixes/ShadowmapCascadeCullingFix.h"
#include "EngineFixes/ShadowmapCascadeRasterizerFix.h"
#include "EngineFixes/WaterReflectionCubemapFix.h"

const std::vector<EngineFix*>& EngineFix::GetOnPostPostLoadFixesList()
{
	static EffectShaderNoDecalsFix effectShaderNoDecalsFix;
	static ShadowmapCascadeCullingFix shadowmapCascadeCullingFix;
	static ShadowmapRasterizerFix shadowmapRasterizerFix;
	static WaterReflectionCubemapFix waterReflectionCubemapFix;

	static std::vector<EngineFix*> fixes = {
		&effectShaderNoDecalsFix,
		&LandscapeSeamsFix::GetInstance(),
		&shadowmapCascadeCullingFix,
		&shadowmapRasterizerFix,
		&waterReflectionCubemapFix
	};

	return fixes;
}

const std::vector<EngineFix*>& EngineFix::GetOnDataLoadedFixesList()
{
	static std::vector<EngineFix*> fixes = {};

	return fixes;
}

void EngineFix::InstallFixes(const std::vector<EngineFix*>& fixes)
{
	for (const auto fix : fixes) {
		fix->Install();
		logger::info("[Engine Fixes] Installed {}", fix->GetName());
	}
}

void EngineFix::InstallOnPostPostLoadFixes()
{
	InstallFixes(GetOnPostPostLoadFixesList());
}

void EngineFix::InstallOnDataLoadedFixes()
{
	InstallFixes(GetOnDataLoadedFixesList());
}
