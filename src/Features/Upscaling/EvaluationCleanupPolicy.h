#pragma once

#include <cstdint>

enum class EvaluationCleanupAction : uint8_t
{
	kDestroy,
	kDefer,
	kQuarantine,
	kRetainUntilFSRTeardown,
};

/** Selects the conservative lifetime action after a partially completed evaluation. */
[[nodiscard]] constexpr EvaluationCleanupAction SelectEvaluationCleanup(bool a_terminalFault,
	bool a_fsrFrameGeneration, bool a_fsrCanRelease, bool a_submissionMayBeInFlight)
{
	if (a_terminalFault)
		return EvaluationCleanupAction::kQuarantine;
	if (a_fsrFrameGeneration && !a_fsrCanRelease)
		return EvaluationCleanupAction::kRetainUntilFSRTeardown;
	if (a_submissionMayBeInFlight)
		return EvaluationCleanupAction::kDefer;
	return EvaluationCleanupAction::kDestroy;
}
