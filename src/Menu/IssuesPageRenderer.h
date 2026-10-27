#pragma once

#include <cstddef>

/**
 * @brief Collects the issues the Home page's Needs Attention card shows: feature issues (rejected, obsolete or
 * mismatched feature files, failed Baselines) and the setting constraints features currently force on each other.
 *
 * Counts are refreshed at most every ThemeManager::Constants::ISSUES_REFRESH_SECONDS, so the header, footer and
 * Home dashboard can ask for them every frame.
 */
namespace IssuesPageRenderer
{
	/** @brief Number of feature issues FeatureIssues tracks. */
	size_t GetFeatureIssueCount();

	/** @brief Number of settings another feature currently constrains. */
	size_t GetConstraintCount();

	/** @brief Feature issues plus active constraints. */
	size_t GetIssueCount();

	/** @brief True when there are feature issues or active constraints. */
	bool HasIssues();

	/** @brief Draws the Active Setting Constraints label, description and table; each "Constrained By" cell opens its first source feature. */
	void RenderConstraintsSection();
}
