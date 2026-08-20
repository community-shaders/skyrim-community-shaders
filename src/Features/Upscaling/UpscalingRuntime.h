#pragma once

#include "DXVKInterop.h"
#include "DxvkControl.h"
#include "FrameGenController.h"
#include "Streamline.h"

/** Process-lifetime composition root for upscaling and frame generation. */
class UpscalingRuntime
{
	friend struct Upscaling;
	friend class StreamlineSession;
public:
	UpscalingRuntime();
	struct Capabilities
	{
		bool dlss = false;
		bool xess = false;
		bool fsr = false;
		bool reflex = false;
		bool dlssg = false;
		bool fsrfg = false;
	};

	struct FrameGenerationStatus
	{
		FrameGen::Method desired = FrameGen::Method::kNone;
		bool presenterReady = false;
		bool dispatchFaulted = false;
		bool submissionFaulted = false;
	};

	[[nodiscard]] Capabilities GetCapabilities() const;
	[[nodiscard]] FrameGenerationStatus GetFrameGenerationStatus(bool a_hdr) const;
	[[nodiscard]] StreamlineSession::EvaluationResult EvaluateUpscaler(const StreamlineSession::UpscaleRequest& a_request)
	{
		return streamline.EvaluateUpscaler(a_request);
	}
	// Narrow entry points used by the Vulkan-present callback thunks.
	void CompleteFSRSwapchainTeardown()
	{
		vulkan.ReleaseRetainedPresentResourcesAfterFSRSwapchainTeardown();
	}
	[[nodiscard]] bool TrackPresentInputCompletion(
		uint64_t a_generation, VkSemaphore a_semaphore, uint64_t a_value)
	{
		return vulkan.TrackInputCompletion(a_generation, a_semaphore, a_value);
	}

private:
	StreamlineSession& Session() { return streamline; }
	const StreamlineSession& Session() const { return streamline; }
	VulkanDeviceContext& Vulkan() { return vulkan; }
	const VulkanDeviceContext& Vulkan() const { return vulkan; }
	FrameGen::FrameGenerationCoordinator& FrameGeneration() { return frameGeneration; }
	const FrameGen::FrameGenerationCoordinator& FrameGeneration() const { return frameGeneration; }
	DxvkControl& Dxvk() { return dxvk; }
	const DxvkControl& Dxvk() const { return dxvk; }

	VulkanDeviceContext vulkan;
	DxvkControl dxvk;
	StreamlineSession streamline;
	FrameGen::FrameGenerationCoordinator frameGeneration;
};
