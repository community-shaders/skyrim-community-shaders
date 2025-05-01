#include "BugFix.h"

#include "BugFixes/ShadowmapCascadeCullingFix.h"

const std::vector<BugFix*>& BugFix::GetBugFixList()
{
	static ShadowmapCascadeCullingFix shadowmapCascadeCullingFix;

	static std::vector<BugFix*> bugFixes = {
		&shadowmapCascadeCullingFix
	};

	return bugFixes;
}

void BugFix::InstallBugFixes()
{
	for (const auto bugFixes = GetBugFixList(); const auto bugFix : bugFixes) {
		bugFix->Install();
		logger::info("[Bug Fixes] Installed {}", bugFix->GetName());
	}
}
