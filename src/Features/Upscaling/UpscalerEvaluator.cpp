#include "UpscalerEvaluator.h"

#include "StreamlineSdk.h"

StreamlineSession::EvaluationResult UpscalerEvaluator::Evaluate(
	const StreamlineSession::UpscaleRequest& a_request) const
{
	const auto& resources = a_request.resources;
	const auto& dimensions = a_request.dimensions;
	const auto& options = a_request.options;
	switch (a_request.upscaler) {
	case StreamlineSession::Upscaler::kDLSS:
		return session.EvaluateDLSS(resources.colorIn, resources.colorOut, resources.depth, resources.motionVectors,
			dimensions.renderWidth, dimensions.renderHeight, dimensions.outputWidth, dimensions.outputHeight,
			options.qualityMode, options.jitterX, options.jitterY);
	case StreamlineSession::Upscaler::kXeSS:
		return session.EvaluateXeSS(resources.colorIn, resources.colorOut, resources.depth, resources.motionVectors,
			dimensions.renderWidth, dimensions.renderHeight, dimensions.outputWidth, dimensions.outputHeight,
			options.qualityMode, options.sharpness, options.jitterX, options.jitterY);
	case StreamlineSession::Upscaler::kFSR:
		return session.EvaluateFSR(resources.colorIn, resources.colorOut, resources.depth, resources.motionVectors,
			dimensions.renderWidth, dimensions.renderHeight, dimensions.outputWidth, dimensions.outputHeight,
			options.qualityMode, options.sharpness, options.jitterX, options.jitterY);
	}
	return StreamlineSession::EvaluationResult::kFailed;
}

StreamlineSession::EvaluationResult UpscalerEvaluator::Classify(
	int32_t a_result, bool a_outputReady, bool a_skipped)
{
	if (a_result != static_cast<int32_t>(sl::Result::eOk))
		return StreamlineSession::EvaluationResult::kFailed;
	if (a_outputReady)
		return StreamlineSession::EvaluationResult::kReady;
	return a_skipped ? StreamlineSession::EvaluationResult::kSkipped :
	                   StreamlineSession::EvaluationResult::kFailed;
}
