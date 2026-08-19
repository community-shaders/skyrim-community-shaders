#include "DxvkControl.h"

#include "../../DxvkLoader.h"

DxvkControl::DxvkControl() : operations{
	&DxvkLoader::SetSynchronousPresent,
	&DxvkLoader::SetPresentQueueDepth,
	&DxvkLoader::RequestSwapchainRecreate,
} {}

void DxvkControl::RequestSwapchainRecreate(const char* a_reason) const
{
	if (operations.requestSwapchainRecreate && operations.requestSwapchainRecreate())
		logger::info("[UpscalingRuntime] requested DXVK swapchain recreate ({})", a_reason);
	else
		logger::warn("[UpscalingRuntime] dxvkRequestSwapchainRecreate unavailable - {} cannot take effect", a_reason);
}

void DxvkControl::SetSynchronousPresent(bool a_sync)
{
	const int requested = a_sync ? 1 : 0;
	if (appliedSync.load(std::memory_order_acquire) == requested)
		return;
	if (operations.setSynchronousPresent && operations.setSynchronousPresent(a_sync)) {
		appliedSync.store(requested, std::memory_order_release);
		logger::info("[UpscalingRuntime] DXVK synchronous present {}", a_sync ? "enabled" : "disabled");
	} else if (!warnedSyncUnavailable.exchange(true, std::memory_order_acq_rel)) {
		logger::warn("[UpscalingRuntime] dxvkSetSyncPresent unavailable - synchronous present control inactive");
	}
}

void DxvkControl::SetPresentQueuePolicy(PresentQueuePolicy a_policy)
{
	const uint32_t depth = static_cast<uint32_t>(a_policy);
	if (appliedQueueDepth.load(std::memory_order_acquire) == depth)
		return;
	if (operations.setPresentQueueDepth && operations.setPresentQueueDepth(depth)) {
		appliedQueueDepth.store(depth, std::memory_order_release);
		if (a_policy == PresentQueuePolicy::kUnrestricted)
			logger::info("[UpscalingRuntime] DXVK present queue depth unrestricted");
		else
			logger::info("[UpscalingRuntime] DXVK present queue depth set to {}", depth);
	} else {
		SetSynchronousPresent(a_policy == PresentQueuePolicy::kSynchronous);
	}
}
