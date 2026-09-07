#include "SceneFeatureReplica.h"

#include "../I18n/I18n.h"
#include "Feature.h"
#include "SceneWidgetInterceptor.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	/// The failure is a session-wide fact, so it is logged once rather than every frame it is shown.
	bool loggedInstallFailure = false;

	void DrawInstallFailure()
	{
		const auto failed = SceneWidgetInterceptor::GetInstallError();
		const std::string entryPoint = failed.empty() ? "install" : std::string{ failed };

		if (!loggedInstallFailure) {
			loggedInstallFailure = true;
			logger::error(
				"Scene Manager authoring is unavailable: ImGui interception failed at '{}'", entryPoint);
		}
		Util::Text::WrappedError(
			T(TKEY("scene_interception_failed"),
				"Scene Manager authoring is unavailable: ImGui interception failed at '%s'. "
				"Scene settings cannot be edited for the rest of this session. Restart the game, "
				"and report this if it persists."),
			entryPoint.c_str());
	}
}

void SceneFeatureReplica::Draw(const std::string& featureShortName,
	const SceneSettingsManager::SceneContextId& contextId, bool perPeriod)
{
	if (!SceneWidgetInterceptor::IsInstalled()) {
		DrawInstallFailure();
		return;
	}

	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature) {
		Util::Text::WrappedDisabled("%s",
			T(TKEY("scene_feature_unloaded"), "This feature is not loaded, so it has nothing to edit."));
		return;
	}

	// No SceneLayerGuard here: the replica must show the live, scene-applied values so an active
	// override reads back as the value it applies.
	const SceneWidgetInterceptor::Scope scope({ feature, contextId, perPeriod });
	feature->DrawSettings();
}

#undef I18N_KEY_PREFIX
