#pragma once

#include "Streamline.h"

/** Dispatches regular upscaler requests while StreamlineSession remains the public facade. */
class UpscalerEvaluator
{
public:
	explicit UpscalerEvaluator(StreamlineSession& a_session) : session(a_session) {}

	[[nodiscard]] StreamlineSession::EvaluationResult Evaluate(
		const StreamlineSession::UpscaleRequest& a_request) const;
	[[nodiscard]] static StreamlineSession::EvaluationResult Classify(
		int32_t a_result, bool a_outputReady, bool a_skipped);

private:
	StreamlineSession& session;
};
