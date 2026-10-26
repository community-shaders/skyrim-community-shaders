#pragma once

#include <cstddef>

/**
 * @brief The Issues page: feature issues (rejected, obsolete or mismatched feature files, failed Baselines)
 * and the setting constraints features currently force on each other.
 *
 * Counts are refreshed at most every ThemeManager::Constants::ISSUES_REFRESH_SECONDS, so the nav entry and
 * the Home dashboard can ask for them every frame.
 */
namespace IssuesPageRenderer
{
	/** @brief Number of feature issues FeatureIssues tracks. */
	size_t GetFeatureIssueCount();

	/** @brief Number of settings another feature currently constrains. */
	size_t GetConstraintCount();

	/** @brief Feature issues plus active constraints: the count the nav entry shows. */
	size_t GetIssueCount();

	/** @brief True when either section of the page has something to show, which is when its nav entry appears. */
	bool HasIssues();

	/** @brief Draws the page: a header, the feature issues, then the active setting constraints table. */
	void Render();
}
