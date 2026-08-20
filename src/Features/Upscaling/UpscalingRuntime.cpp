#include "UpscalingRuntime.h"

UpscalingRuntime::UpscalingRuntime() : streamline(vulkan), frameGeneration(vulkan, streamline, dxvk) {}

UpscalingRuntime::Capabilities UpscalingRuntime::GetCapabilities() const
{
	return {
		.dlss = streamline.IsDLSSSupported(),
		.xess = streamline.IsXeSSSupported(),
		.fsr = streamline.IsFSRSupported(),
		.reflex = streamline.IsReflexSupported(),
		.dlssg = streamline.IsDLSSGSupported(),
		.fsrfg = streamline.IsFSRFGSupported(),
	};
}

UpscalingRuntime::FrameGenerationStatus UpscalingRuntime::GetFrameGenerationStatus(bool a_hdr) const
{
	return {
		.desired = frameGeneration.GetDesiredMethod(),
		.presenterReady = vulkan.IsPresenterStateReadyForFrame(a_hdr),
		.dispatchFaulted = streamline.HasDispatchFaulted(),
		.submissionFaulted = vulkan.HasCommandRingFault(),
	};
}
