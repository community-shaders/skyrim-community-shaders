#include "DXVKInterop.h"

#include "Globals.h"

#include <algorithm>

namespace
{
	struct QueueSubmitAttempt
	{
		VkResult endResult = VK_ERROR_DEVICE_LOST;
		VkResult resetResult = VK_ERROR_DEVICE_LOST;
		VkResult submitResult = VK_ERROR_DEVICE_LOST;
		/// Present-wait generation returned by dxvkEnqueueInteropCommandBuffer; 0 means DXVK
		/// rejected the submission. Non-zero also covers the no-semaphore case (DXVK returns 1).
		uint64_t generation = 0;
		DWORD exceptionCode = 0;
		bool queueLockAttempted = false;
		bool queueLockAcquired = false;
		bool queueReleaseCompleted = false;
		bool faulted = false;
	};

	struct VulkanResultAttempt
	{
		VkResult result = VK_ERROR_DEVICE_LOST;
		DWORD exceptionCode = 0;
	};

	struct VulkanVoidAttempt
	{
		DWORD exceptionCode = 0;
		bool completed = false;
	};

	struct PresentWaitStateAttempt
	{
		uint32_t state = 0;
		DWORD exceptionCode = 0;
	};

	VulkanResultAttempt CreateCommandPoolSEH(VkDevice a_device,
		const VkCommandPoolCreateInfo* a_createInfo, VkCommandPool* a_commandPool) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkCreateCommandPool(a_device, a_createInfo, nullptr, a_commandPool);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanResultAttempt AllocateCommandBuffersSEH(VkDevice a_device,
		const VkCommandBufferAllocateInfo* a_allocateInfo, VkCommandBuffer* a_commandBuffers) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkAllocateCommandBuffers(a_device, a_allocateInfo, a_commandBuffers);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanResultAttempt CreateSemaphoreSEH(VkDevice a_device,
		const VkSemaphoreCreateInfo* a_createInfo, VkSemaphore* a_semaphore) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkCreateSemaphore(a_device, a_createInfo, nullptr, a_semaphore);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanResultAttempt GetFenceStatusSEH(VkDevice a_device, VkFence a_fence) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkGetFenceStatus(a_device, a_fence);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanResultAttempt WaitForFenceSEH(VkDevice a_device, VkFence a_fence) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkWaitForFences(a_device, 1, &a_fence, VK_TRUE, UINT64_MAX);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanResultAttempt ResetCommandBufferSEH(VkCommandBuffer a_commandBuffer) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkResetCommandBuffer(a_commandBuffer, 0);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanResultAttempt BeginCommandBufferSEH(VkCommandBuffer a_commandBuffer,
		const VkCommandBufferBeginInfo* a_beginInfo) noexcept
	{
		VulkanResultAttempt attempt{};
		__try {
			attempt.result = vkBeginCommandBuffer(a_commandBuffer, a_beginInfo);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanVoidAttempt DestroyImageViewSEH(PFN_vkDestroyImageView a_destroyImageView,
		VkDevice a_device, VkImageView a_view) noexcept
	{
		VulkanVoidAttempt attempt{};
		__try {
			if (a_destroyImageView && a_view != VK_NULL_HANDLE)
				a_destroyImageView(a_device, a_view, nullptr);
			attempt.completed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanVoidAttempt DestroySemaphoreSEH(VkDevice a_device, VkSemaphore a_semaphore) noexcept
	{
		VulkanVoidAttempt attempt{};
		__try {
			if (a_semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(a_device, a_semaphore, nullptr);
			attempt.completed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanVoidAttempt DestroyCommandPoolSEH(VkDevice a_device, VkCommandPool a_commandPool) noexcept
	{
		VulkanVoidAttempt attempt{};
		__try {
			if (a_commandPool != VK_NULL_HANDLE)
				vkDestroyCommandPool(a_device, a_commandPool, nullptr);
			attempt.completed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VulkanVoidAttempt FreeCommandBuffersSEH(VkDevice a_device, VkCommandPool a_commandPool,
		uint32_t a_count, const VkCommandBuffer* a_commandBuffers) noexcept
	{
		VulkanVoidAttempt attempt{};
		__try {
			if (a_count)
				vkFreeCommandBuffers(a_device, a_commandPool, a_count, a_commandBuffers);
			attempt.completed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}


	PresentWaitStateAttempt GetPresentWaitSemaphoreStateSEH(
		uint32_t (*a_getState)(uint64_t), uint64_t a_generation) noexcept
	{
		PresentWaitStateAttempt attempt{};
		__try {
			if (a_getState)
				attempt.state = a_getState(a_generation);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	PresentWaitStateAttempt ClearPresentWaitSemaphoreSEH(
		uint32_t (*a_clear)(uint64_t), uint64_t a_generation) noexcept
	{
		PresentWaitStateAttempt attempt{};
		__try {
			if (a_clear)
				attempt.state = a_clear(a_generation);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	PresentWaitStateAttempt CancelPresentWaitSemaphoreSEH(
		uint32_t (*a_cancel)(VkSemaphore), VkSemaphore a_semaphore) noexcept
	{
		PresentWaitStateAttempt attempt{};
		__try {
			if (a_cancel)
				attempt.state = a_cancel(a_semaphore);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	struct QueueReleaseAttempt
	{
		DWORD exceptionCode = 0;
		bool completed = false;
	};

	QueueReleaseAttempt ReleaseSubmissionQueueSEH(IDXGIVkInteropDevice* a_interopDevice) noexcept
	{
		QueueReleaseAttempt attempt{};
		__try {
			a_interopDevice->ReleaseSubmissionQueue();
			attempt.completed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	/// Hands the recorded command buffer to DXVK instead of submitting it ourselves.
	///
	/// The previous implementation took DXVK's submission-queue lock and called
	/// vkQueueSubmit directly on DXVK's queue. That is why this file carries so much
	/// machinery around "foreign queue submission": a fault between Lock and Release
	/// leaks DXVK's lock, and a fault after vkQueueSubmit accepted the work leaves the
	/// fence, command buffer and semaphore in an indeterminate state.
	///
	/// dxvkEnqueueInteropCommandBuffer removes that whole class of hazard. DXVK pushes
	/// the command buffer onto its own submission thread, which already owns the queue,
	/// and registers the signal semaphore in the present-wait FIFO in the same call, so
	/// there is no separate push step that could land out of order with the submit.
	QueueSubmitAttempt EnqueueInteropSEH(IDXGIVkInteropDevice* a_interopDevice, VkDevice a_device,
		VkCommandBuffer a_commandBuffer, VkFence a_fence, VkSemaphore a_signalSemaphore,
		uint64_t (*a_enqueue)(VkCommandBuffer, VkSemaphore, VkFence)) noexcept
	{
		QueueSubmitAttempt attempt{};
		__try {
			attempt.endResult = vkEndCommandBuffer(a_commandBuffer);
			if (attempt.endResult == VK_SUCCESS) {
				attempt.resetResult = vkResetFences(a_device, 1, &a_fence);
				if (attempt.resetResult == VK_SUCCESS) {
					// Flush D3D11 work first so our command buffer is ordered after it.
					a_interopDevice->FlushRenderingCommands();
					attempt.generation = a_enqueue
						? a_enqueue(a_commandBuffer, a_signalSemaphore, a_fence)
						: 0;
					// DXVK returns 0 only if it refused the submission outright (null command
					// buffer/fence, or no free FIFO slot); it never partially submits.
					attempt.submitResult = attempt.generation ? VK_SUCCESS : VK_ERROR_UNKNOWN;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.faulted = true;
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	struct DeviceIdleAttempt
	{
		VkResult result = VK_ERROR_INITIALIZATION_FAILED;
		DWORD exceptionCode = 0;
		bool queueLockAttempted = false;
		bool queueLockAcquired = false;
		bool queueReleaseCompleted = false;
		bool functionAvailable = false;
		bool presentWaitReleaseAttempted = false;
		bool presentWaitReleaseCompleted = false;
		uint32_t releasedPresentWaitCount = 0;
		bool faulted = false;
	};

	DeviceIdleAttempt WaitDeviceIdleSEH(IDXGIVkInteropDevice* a_interopDevice,
		PFN_vkGetDeviceProcAddr a_getDeviceProcAddr, VkDevice a_device,
		uint32_t (*a_releaseQueuedPresentWaits)()) noexcept
	{
		DeviceIdleAttempt attempt{};
		__try {
			a_interopDevice->FlushRenderingCommands();
			__try {
				attempt.queueLockAttempted = true;
				a_interopDevice->LockSubmissionQueue();
				attempt.queueLockAcquired = true;
				auto waitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
					a_getDeviceProcAddr(a_device, "vkDeviceWaitIdle"));
				attempt.functionAvailable = waitIdle != nullptr;
				if (waitIdle) {
					attempt.result = waitIdle(a_device);
					if (attempt.result == VK_SUCCESS && a_releaseQueuedPresentWaits) {
						attempt.presentWaitReleaseAttempted = true;
						attempt.releasedPresentWaitCount = a_releaseQueuedPresentWaits();
						attempt.presentWaitReleaseCompleted = true;
					}
				}
			} __finally {
				if (attempt.queueLockAcquired) {
					const QueueReleaseAttempt release = ReleaseSubmissionQueueSEH(a_interopDevice);
					attempt.queueReleaseCompleted = release.completed;
					if (!release.completed) {
						attempt.faulted = true;
						attempt.exceptionCode = release.exceptionCode;
					}
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			attempt.faulted = true;
			attempt.exceptionCode = GetExceptionCode();
		}
		return attempt;
	}

	VkResult CreateSignaledFenceSEH(VkDevice a_device, VkFence* a_fence, DWORD* a_exceptionCode) noexcept
	{
		VkResult result = VK_ERROR_DEVICE_LOST;
		*a_fence = VK_NULL_HANDLE;
		*a_exceptionCode = 0;
		__try {
			VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			result = vkCreateFence(a_device, &fenceInfo, nullptr, a_fence);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			*a_exceptionCode = GetExceptionCode();
		}
		return result;
	}

	bool DestroyFenceSEH(VkDevice a_device, VkFence a_fence) noexcept
	{
		bool destroyed = false;
		__try {
			if (a_fence != VK_NULL_HANDLE)
				vkDestroyFence(a_device, a_fence, nullptr);
			destroyed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
		return destroyed;
	}
}

DXVKInterop* DXVKInterop::GetSingleton()
{
	static DXVKInterop singleton;
	return &singleton;
}

VkColorSpaceKHR DXVKInterop::RequestedPresenterColorSpace(bool a_hdr)
{
	return a_hdr ? VK_COLOR_SPACE_HDR10_ST2084_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
}

DXVKInterop::PresenterEncoding DXVKInterop::ClassifyPresenterEncoding(const PresenterSurfaceState& a_state)
{
	if (!a_state.serial)
		return PresenterEncoding::kUnknown;

	if (a_state.requestedColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
		a_state.effectiveColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		return PresenterEncoding::kSDR;

	if (a_state.requestedColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
		if (a_state.effectiveColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
			return PresenterEncoding::kHDR10;
		if (a_state.effectiveColorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
			return PresenterEncoding::kHDR10ScRGBFallback;
	}

	return PresenterEncoding::kUnknown;
}

bool DXVKInterop::PresenterStateMatches(
	const PresenterSurfaceState& a_state, VkColorSpaceKHR a_requestedColorSpace)
{
	if (!a_state.serial || a_state.requestedColorSpace != a_requestedColorSpace)
		return false;

	const PresenterEncoding encoding = ClassifyPresenterEncoding(a_state);
	if (a_requestedColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
		return encoding == PresenterEncoding::kHDR10 ||
		       encoding == PresenterEncoding::kHDR10ScRGBFallback;
	return a_requestedColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
	       encoding == PresenterEncoding::kSDR;
}

bool DXVKInterop::RefreshPresenterSurfaceState()
{
	if (!getPresenterSurfaceState)
		return false;

	uint32_t format = VK_FORMAT_UNDEFINED;
	uint32_t requestedColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	uint32_t effectiveColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	const uint64_t serial = getPresenterSurfaceState(&format, &requestedColorSpace, &effectiveColorSpace);
	if (!serial)
		return false;

	std::lock_guard lock(presenterStateMutex);
	if (serial <= observedPresenterState.serial)
		return false;

	observedPresenterState.serial = serial;
	observedPresenterState.format = static_cast<VkFormat>(format);
	observedPresenterState.requestedColorSpace = static_cast<VkColorSpaceKHR>(requestedColorSpace);
	observedPresenterState.effectiveColorSpace = static_cast<VkColorSpaceKHR>(effectiveColorSpace);
	logger::info("[DXVKInterop] Observed presenter surface serial {}: format={}, requestedColorSpace={}, effectiveColorSpace={}",
		serial, format, requestedColorSpace, effectiveColorSpace);
	return true;
}

void DXVKInterop::CommitPresenterSurfaceStateForRenderFrame()
{
	std::lock_guard lock(presenterStateMutex);

	// The pending-transition check has to run before the no-new-serial early return below.
	// With it placed after that return, the timeout only ticked on frames where a new serial
	// had already been observed - which is exactly the case that does not need a timeout. A
	// transition waiting on a serial that never arrives (a stale swapchain, or one created
	// before the observation hook attached) returned early every frame, never counted, and
	// stayed pending for the session, pinning IsPresenterStateReadyForFrame() false and with
	// it IsFrameGenerationActive(). The counter has to run on every frame the transition is
	// unsatisfied, whatever the reason.
	if (presenterTransitionPending) {
		const bool satisfied =
			observedPresenterState.serial > presenterTransitionBaselineSerial &&
			observedPresenterState.requestedColorSpace == presenterTransitionRequestedColorSpace;
		if (!satisfied) {
			if (++presenterTransitionFrameCount > 120) {
				logger::warn("[DXVKInterop] presenter color-space transition timed out after {} frames; cancelling",
					presenterTransitionFrameCount);
				presenterTransitionPending = false;
				presenterTransitionFrameCount = 0;
			}
			return;
		}
	}

	if (!observedPresenterState.serial ||
		observedPresenterState.serial <= committedPresenterState.serial)
		return;

	if (presenterTransitionPending) {
		presenterTransitionPending = false;
		presenterTransitionFrameCount = 0;
	}

	committedPresenterState = observedPresenterState;
	logger::info("[DXVKInterop] Committed presenter surface serial {} for render frames",
		committedPresenterState.serial);
	if (ClassifyPresenterEncoding(committedPresenterState) == PresenterEncoding::kHDR10ScRGBFallback) {
		logger::warn("[DXVKInterop] HDR frame generation disabled for the scRGB presenter fallback; "
		             "a HUD-less image rendered directly in the presenter encoding is required");
	}
}

void DXVKInterop::BeginPresenterColorSpaceTransition(bool a_hdr, bool a_requireNewSerial)
{
	const VkColorSpaceKHR requestedColorSpace = RequestedPresenterColorSpace(a_hdr);
	std::lock_guard lock(presenterStateMutex);

	if (presenterTransitionPending &&
		presenterTransitionRequestedColorSpace == requestedColorSpace)
		return;

	if (!presenterTransitionPending && !a_requireNewSerial &&
		(PresenterStateMatches(committedPresenterState, requestedColorSpace) ||
		 PresenterStateMatches(observedPresenterState, requestedColorSpace)))
		return;

	presenterTransitionPending = true;
	presenterTransitionFrameCount = 0;
	presenterTransitionRequestedColorSpace = requestedColorSpace;
	presenterTransitionBaselineSerial = observedPresenterState.serial > committedPresenterState.serial
	                                    ? observedPresenterState.serial
	                                    : committedPresenterState.serial;
	logger::info("[DXVKInterop] Presenter color-space transition started: target={}, baselineSerial={}",
		static_cast<uint32_t>(requestedColorSpace), presenterTransitionBaselineSerial);
}

void DXVKInterop::CancelPresenterColorSpaceTransition(bool a_hdr)
{
	const VkColorSpaceKHR requestedColorSpace = RequestedPresenterColorSpace(a_hdr);
	std::lock_guard lock(presenterStateMutex);
	if (presenterTransitionPending &&
		presenterTransitionRequestedColorSpace == requestedColorSpace) {
		presenterTransitionPending = false;
		logger::warn("[DXVKInterop] Presenter color-space transition cancelled for target={}",
			static_cast<uint32_t>(requestedColorSpace));
	}
}

DXVKInterop::PresenterEncoding DXVKInterop::GetPresenterEncodingForFrame() const
{
	std::lock_guard lock(presenterStateMutex);
	return presenterTransitionPending ? PresenterEncoding::kUnknown :
	                                    ClassifyPresenterEncoding(committedPresenterState);
}

VkFormat DXVKInterop::GetPresenterFormatForFrame() const
{
	std::lock_guard lock(presenterStateMutex);
	return presenterTransitionPending ? VK_FORMAT_UNDEFINED : committedPresenterState.format;
}

bool DXVKInterop::IsPresenterStateReadyForFrame(bool a_hdr) const
{
	std::lock_guard lock(presenterStateMutex);
	if (presenterTransitionPending ||
		!PresenterStateMatches(committedPresenterState, RequestedPresenterColorSpace(a_hdr)))
		return false;
	return ClassifyPresenterEncoding(committedPresenterState) != PresenterEncoding::kHDR10ScRGBFallback;
}

bool DXVKInterop::Initialize()
{
	if (available)
		return true;

	auto d3dDevice = globals::d3d::device;
	if (!d3dDevice) {
		logger::warn("[DXVKInterop] No D3D11 device available yet");
		return false;
	}

	winrt::com_ptr<IDXGIVkInteropDevice> dev;
	if (FAILED(d3dDevice->QueryInterface(__uuidof(IDXGIVkInteropDevice), dev.put_void()))) {
		logger::info("[DXVKInterop] IDXGIVkInteropDevice not present — not running under DXVK");
		return false;
	}

	interopDevice = dev;
	interopDevice->GetVulkanHandles(&instance, &physicalDevice, &device);
	interopDevice->GetSubmissionQueue(&queue, &queueFamilyIndex);

	if (!instance || !physicalDevice || !device || !queue) {
		logger::error("[DXVKInterop] DXVK returned null Vulkan handles");
		interopDevice = nullptr;
		return false;
	}

	if (HMODULE vk = GetModuleHandleW(L"vulkan-1.dll")) {
		vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			reinterpret_cast<void*>(GetProcAddress(vk, "vkGetInstanceProcAddr")));
	}
	if (!vkGetInstanceProcAddr) {
		logger::error("[DXVKInterop] Could not resolve vkGetInstanceProcAddr from vulkan-1.dll");
		interopDevice = nullptr;
		return false;
	}

	vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
		vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
	if (!vkGetDeviceProcAddr) {
		logger::error("[DXVKInterop] Could not resolve vkGetDeviceProcAddr");
		interopDevice = nullptr;
		return false;
	}

	vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
		vkGetDeviceProcAddr(device, "vkDestroyImageView"));

	if (HMODULE module = GetModuleHandleW(L"dxvk_d3d11.dll")) {
		// DXVK submits the interop command buffer through its OWN submission thread and
		// registers the signal semaphore in the present-wait FIFO in one call. This replaces
		// the old submit-here-then-push-separately pair: DXVK takes its queue lock itself, so
		// the whole foreign-queue-submission hazard class goes away.
		enqueueInteropCommandBuffer = reinterpret_cast<EnqueueInteropCommandBufferFn>(
			GetProcAddress(module, "dxvkEnqueueInteropCommandBuffer"));
		getPresentWaitSemaphoreState = reinterpret_cast<uint32_t (*)(uint64_t)>(
			GetProcAddress(module, "dxvkGetPresentWaitSemaphoreState"));
		clearPresentWaitSemaphore = reinterpret_cast<uint32_t (*)(uint64_t)>(
			GetProcAddress(module, "dxvkClearPresentWaitSemaphore"));
		cancelPresentWaitSemaphore = reinterpret_cast<uint32_t (*)(VkSemaphore)>(
			GetProcAddress(module, "dxvkCancelPresentWaitSemaphore"));
		releaseQueuedPresentWaitSemaphoresAfterIdle = reinterpret_cast<uint32_t (*)()>(
			GetProcAddress(module, "dxvkReleaseQueuedPresentWaitSemaphoresAfterIdle"));
		synchronousPresentControlAvailable = GetProcAddress(module, "dxvkSetSyncPresent") != nullptr;
		getPresenterSurfaceState = reinterpret_cast<GetPresenterSurfaceStateFn>(
			GetProcAddress(module, "dxvkGetPresenterSurfaceState"));
	}
	char splitValue[2]{};
	presentQueueSplit = GetEnvironmentVariableA("DXVK_PRESENT_QUEUE_SPLIT", splitValue,
		static_cast<DWORD>(std::size(splitValue))) != 0 && splitValue[0] == '1';
	if (!enqueueInteropCommandBuffer || !getPresentWaitSemaphoreState || !clearPresentWaitSemaphore ||
		!cancelPresentWaitSemaphore || !releaseQueuedPresentWaitSemaphoresAfterIdle)
		logger::warn("[DXVKInterop] acknowledged present-wait semaphore interop is unavailable - DLSS-G disabled");
	if (!synchronousPresentControlAvailable)
		logger::warn("[DXVKInterop] dxvkSetSyncPresent is unavailable - DLSS-G disabled");
	if (!getPresenterSurfaceState)
		logger::warn("[DXVKInterop] dxvkGetPresenterSurfaceState is unavailable - frame generation disabled");
	if (presentQueueSplit)
		logger::warn("[DXVKInterop] DXVK_PRESENT_QUEUE_SPLIT is incompatible with reusable DLSS-G present semaphores");

	if (auto pfnProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
			vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"))) {
		VkPhysicalDeviceProperties props{};
		pfnProps(physicalDevice, &props);
		logger::info("[DXVKInterop] Bridged to DXVK Vulkan device: '{}' (API {}.{}.{}), queueFamily {}",
			props.deviceName,
			VK_API_VERSION_MAJOR(props.apiVersion),
			VK_API_VERSION_MINOR(props.apiVersion),
			VK_API_VERSION_PATCH(props.apiVersion),
			queueFamilyIndex);
	} else {
		logger::info("[DXVKInterop] Bridged to DXVK Vulkan device (queueFamily {})", queueFamilyIndex);
	}

	available = true;
	return true;
}

bool DXVKInterop::GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
	VkImageLayout* a_outLayout, VkImageCreateInfo* a_outInfo) const
{
	if (!available || !a_resource)
		return false;

	winrt::com_ptr<IDXGIVkInteropSurface> surface;
	if (FAILED(a_resource->QueryInterface(__uuidof(IDXGIVkInteropSurface), surface.put_void())))
		return false;

	VkImageCreateInfo localInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkImageCreateInfo* info = a_outInfo ? a_outInfo : &localInfo;
	info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info->pNext = nullptr;
	info->queueFamilyIndexCount = 0;
	info->pQueueFamilyIndices = nullptr;

	return SUCCEEDED(surface->GetVulkanImageInfo(a_outImage, a_outLayout, info));
}

bool DXVKInterop::WaitDeviceIdle()
{
	std::lock_guard lock(commandRingMutex);
	if (!interopDevice || !vkGetDeviceProcAddr || device == VK_NULL_HANDLE)
		return false;
	// A lost device never comes back, so every caller that waits on idle to prove completion will
	// fail forever. Returning early keeps that from becoming a per-frame retry: the callers above
	// this one re-enter on the next frame, and without the latch a single loss produced 243k
	// vkDeviceWaitIdle(-4) lines and 638k log lines in one session while the game hammered a dead
	// device. Fail fast and silently instead; the loss itself is reported once, below.
	if (deviceLost)
		return false;
	if (submissionQueueLockUncertain) {
		logger::error("[DXVKInterop] refusing device-idle synchronization because the submission queue lock state is uncertain");
		return false;
	}

	const DeviceIdleAttempt attempt = WaitDeviceIdleSEH(interopDevice.get(), vkGetDeviceProcAddr, device,
		presentWaitInteropTerminalFault ? nullptr : releaseQueuedPresentWaitSemaphoresAfterIdle);
	if (attempt.queueLockAttempted &&
		(!attempt.queueLockAcquired || !attempt.queueReleaseCompleted)) {
		submissionQueueLockUncertain = true;
		logger::error("[DXVKInterop] submission queue lock state became uncertain during device-idle synchronization");
	}
	if (attempt.presentWaitReleaseAttempted && !attempt.presentWaitReleaseCompleted) {
		commandRingSubmissionsIdleProven = attempt.result == VK_SUCCESS;
		commandRingFaulted = true;
		presentWaitInteropTerminalFault = true;
		enqueueInteropCommandBuffer = nullptr;
		logger::critical("[DXVKInterop] queued present-wait release faulted after device idle (SEH {:#x}); present interop is quarantined",
			attempt.exceptionCode);
		return false;
	}
	if (attempt.faulted) {
		logger::error("[DXVKInterop] device-idle synchronization faulted (SEH {:#x})",
			attempt.exceptionCode);
		return false;
	} else if (!attempt.functionAvailable) {
		logger::error("[DXVKInterop] vkDeviceWaitIdle is unavailable");
		return false;
	} else if (attempt.result != VK_SUCCESS) {
		if (attempt.result == VK_ERROR_DEVICE_LOST) {
			deviceLost = true;
			commandRingFaulted = true;
			presentWaitInteropTerminalFault = true;
			enqueueInteropCommandBuffer = nullptr;
			logger::critical("[DXVKInterop] device lost; Vulkan interop is quarantined for this session");
		} else {
			logger::error("[DXVKInterop] vkDeviceWaitIdle failed ({})", static_cast<int>(attempt.result));
		}
		return false;
	}
	commandRingSubmissionsIdleProven = true;
	if (attempt.releasedPresentWaitCount)
		logger::debug("[DXVKInterop] released {} queued present waits after device idle",
			attempt.releasedPresentWaitCount);
	return true;
}

bool DXVKInterop::ClearReleasedPresentWaitsAfterIdle()
{
	if (presentWaitInteropTerminalFault || !getPresentWaitSemaphoreState || !clearPresentWaitSemaphore)
		return false;

	constexpr uint32_t kPresentWaitNone = 0;
	constexpr uint32_t kPresentWaitUncertain = 3;
	constexpr uint32_t kPresentWaitReleased = 4;
	const auto latchTerminalFault = [&](const char* a_operation, DWORD a_exceptionCode = 0) {
		commandRingFaulted = true;
		presentWaitInteropTerminalFault = true;
		enqueueInteropCommandBuffer = nullptr;
		pushedPresentWaitSlot = UINT32_MAX;
		pushedPresentWaitGeneration = 0;
		outstandingPresentWaitSubmissions.clear();
		if (a_exceptionCode) {
			logger::critical("[DXVKInterop] {} faulted (SEH {:#x}); present interop is quarantined",
				a_operation, a_exceptionCode);
		} else {
			logger::critical("[DXVKInterop] {}; present interop is quarantined", a_operation);
		}
	};

	for (size_t i = 0; i < outstandingPresentWaitSubmissions.size();) {
		const PresentWaitSubmission submission = outstandingPresentWaitSubmissions[i];
		const PresentWaitStateAttempt stateAttempt = GetPresentWaitSemaphoreStateSEH(
			getPresentWaitSemaphoreState, submission.generation);
		if (stateAttempt.exceptionCode) {
			latchTerminalFault("idle-released present-wait query", stateAttempt.exceptionCode);
			return false;
		}
		if (stateAttempt.state != kPresentWaitReleased) {
			if (stateAttempt.state == kPresentWaitUncertain || stateAttempt.state == kPresentWaitNone)
				latchTerminalFault("idle-released present-wait state is unsafe");
			return false;
		}
		if (submission.slot >= presentWaitInUse.size()) {
			latchTerminalFault("idle-released present-wait slot is invalid");
			return false;
		}
		const PresentWaitStateAttempt clearAttempt = ClearPresentWaitSemaphoreSEH(
			clearPresentWaitSemaphore, submission.generation);
		if (clearAttempt.exceptionCode || !clearAttempt.state) {
			latchTerminalFault("idle-released present-wait generation could not be cleared",
				clearAttempt.exceptionCode);
			return false;
		}
		presentWaitInUse[submission.slot] = false;
		outstandingPresentWaitSubmissions.erase(outstandingPresentWaitSubmissions.begin() + i);
	}

	if (pushedPresentWaitSlot == UINT32_MAX)
		return true;

	const PresentWaitStateAttempt stateAttempt = GetPresentWaitSemaphoreStateSEH(
		getPresentWaitSemaphoreState, pushedPresentWaitGeneration);
	if (stateAttempt.exceptionCode) {
		latchTerminalFault("idle-released pushed present-wait query", stateAttempt.exceptionCode);
		return false;
	}
	if (stateAttempt.state != kPresentWaitReleased) {
		if (stateAttempt.state == kPresentWaitUncertain || stateAttempt.state == kPresentWaitNone)
			latchTerminalFault("idle-released pushed present-wait state is unsafe");
		return false;
	}
	if (pushedPresentWaitSlot >= presentWaitInUse.size()) {
		latchTerminalFault("idle-released pushed present-wait slot is invalid");
		return false;
	}
	const PresentWaitStateAttempt clearAttempt = ClearPresentWaitSemaphoreSEH(
		clearPresentWaitSemaphore, pushedPresentWaitGeneration);
	if (clearAttempt.exceptionCode || !clearAttempt.state) {
		latchTerminalFault("idle-released pushed present-wait generation could not be cleared",
			clearAttempt.exceptionCode);
		return false;
	}
	presentWaitInUse[pushedPresentWaitSlot] = false;
	pushedPresentWaitSlot = UINT32_MAX;
	pushedPresentWaitGeneration = 0;
	return true;
}

bool DXVKInterop::CreateCommandResources(uint32_t a_framesInFlight)
{
	std::lock_guard lock(commandRingMutex);
	if (!available || vulkanResourceDestructionTerminalFault)
		return false;
	if (submissionQueueLockUncertain)
		return false;
	if (commandPool != VK_NULL_HANDLE)
		return !commandRingFaulted;

	commandRingFaulted = false;

	framesInFlight = a_framesInFlight ? a_framesInFlight : 1;

	VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndex;
	VkCommandPool createdCommandPool = VK_NULL_HANDLE;
	const VulkanResultAttempt poolAttempt = CreateCommandPoolSEH(device, &poolInfo, &createdCommandPool);
	const bool poolCreated = !poolAttempt.exceptionCode && poolAttempt.result == VK_SUCCESS;
	if (!poolCreated) {
		createdCommandPool = VK_NULL_HANDLE;
		commandRingFaulted = true;
		if (poolAttempt.exceptionCode) {
			vulkanResourceDestructionTerminalFault = true;
			logger::error("[DXVKInterop] vkCreateCommandPool faulted (SEH {:#x})",
				poolAttempt.exceptionCode);
		} else {
			logger::error("[DXVKInterop] vkCreateCommandPool failed ({})",
				static_cast<int>(poolAttempt.result));
		}
		return false;
	}
	commandPool = createdCommandPool;

	std::vector<VkCommandBuffer> allocatedCommandBuffers(framesInFlight, VK_NULL_HANDLE);
	VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = framesInFlight;
	const VulkanResultAttempt allocateAttempt =
		AllocateCommandBuffersSEH(device, &allocInfo, allocatedCommandBuffers.data());
	const bool commandBuffersAllocated =
		!allocateAttempt.exceptionCode && allocateAttempt.result == VK_SUCCESS;
	if (!commandBuffersAllocated) {
		std::fill(allocatedCommandBuffers.begin(), allocatedCommandBuffers.end(), VK_NULL_HANDLE);
		if (allocateAttempt.exceptionCode) {
			vulkanResourceDestructionTerminalFault = true;
			logger::error("[DXVKInterop] vkAllocateCommandBuffers faulted (SEH {:#x})",
				allocateAttempt.exceptionCode);
		} else {
			logger::error("[DXVKInterop] vkAllocateCommandBuffers failed ({})",
				static_cast<int>(allocateAttempt.result));
		}
		commandRingFaulted = true;
		if (!allocateAttempt.exceptionCode) {
			DestroyCommandResources();
			commandRingFaulted = true;
		}
		return false;
	}
	commandBuffers = std::move(allocatedCommandBuffers);

	// Fences start signaled so the first BeginFrameCommandBuffer doesn't block.
	commandFences.resize(framesInFlight, VK_NULL_HANDLE);
	for (uint32_t i = 0; i < framesInFlight; ++i) {
		VkFence createdFence = VK_NULL_HANDLE;
		DWORD exceptionCode = 0;
		const VkResult result = CreateSignaledFenceSEH(device, &createdFence, &exceptionCode);
		const bool fenceCreated = !exceptionCode && result == VK_SUCCESS;
		if (!fenceCreated) {
			createdFence = VK_NULL_HANDLE;
			if (exceptionCode) {
				vulkanResourceDestructionTerminalFault = true;
				logger::error("[DXVKInterop] vkCreateFence faulted (SEH {:#x})", exceptionCode);
			} else {
				logger::error("[DXVKInterop] vkCreateFence failed ({})", static_cast<int>(result));
			}
			commandRingFaulted = true;
			if (!exceptionCode) {
				DestroyCommandResources();
				commandRingFaulted = true;
			}
			return false;
		}
		commandFences[i] = createdFence;
	}

	presentWaitSemaphores.resize(framesInFlight, VK_NULL_HANDLE);
	if (PresentWaitInteropReady()) {
		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (uint32_t i = 0; i < framesInFlight; ++i) {
			VkSemaphore createdSemaphore = VK_NULL_HANDLE;
			const VulkanResultAttempt semaphoreAttempt =
				CreateSemaphoreSEH(device, &semaphoreInfo, &createdSemaphore);
			const bool semaphoreCreated =
				!semaphoreAttempt.exceptionCode && semaphoreAttempt.result == VK_SUCCESS;
			if (!semaphoreCreated) {
				createdSemaphore = VK_NULL_HANDLE;
				if (semaphoreAttempt.exceptionCode) {
					commandRingFaulted = true;
					vulkanResourceDestructionTerminalFault = true;
					logger::error("[DXVKInterop] vkCreateSemaphore faulted (SEH {:#x})",
						semaphoreAttempt.exceptionCode);
				} else {
					logger::error("[DXVKInterop] vkCreateSemaphore failed ({}) - DLSS-G present synchronization unavailable",
						static_cast<int>(semaphoreAttempt.result));
				}
				enqueueInteropCommandBuffer = nullptr;
				if (semaphoreAttempt.exceptionCode)
					return false;
				bool destructionFaulted = false;
				for (VkSemaphore& semaphore : presentWaitSemaphores) {
					if (semaphore == VK_NULL_HANDLE)
						continue;
					if (destructionFaulted) {
						semaphore = VK_NULL_HANDLE;
						continue;
					}
					const VulkanVoidAttempt destroyAttempt = DestroySemaphoreSEH(device, semaphore);
					if (!destroyAttempt.completed) {
						commandRingFaulted = true;
						vulkanResourceDestructionTerminalFault = true;
						destructionFaulted = true;
						semaphore = VK_NULL_HANDLE;
					} else {
						semaphore = VK_NULL_HANDLE;
					}
				}
				if (commandRingFaulted) {
					DestroyCommandResources();
					commandRingFaulted = true;
					return false;
				}
				break;
			}
			presentWaitSemaphores[i] = createdSemaphore;
		}
	}
	presentWaitInUse.assign(framesInFlight, false);

	commandFrameIndex = 0;
	pendingViewDeletes.assign(framesInFlight, {});
	pendingResourceReleases.assign(framesInFlight, {});
	logger::info("[DXVKInterop] Command ring created ({} frames in flight, queueFamily {})", framesInFlight, queueFamilyIndex);
	return true;
}

void DXVKInterop::DestroyCommandResources()
{
	std::lock_guard lock(commandRingMutex);
	if (device == VK_NULL_HANDLE)
		return;
	if (vulkanResourceDestructionTerminalFault) {
		logger::error("[DXVKInterop] Vulkan resource cleanup is terminally quarantined after a destruction fault");
		return;
	}
	if (presentWaitInteropTerminalFault && pushedPresentWaitSlot != UINT32_MAX) {
		logger::error("[DXVKInterop] present-wait handle remains quarantined after a terminal bridge fault");
		return;
	}
	const bool hasRegisteredPresentWaits =
		pushedPresentWaitSlot != UINT32_MAX || !outstandingPresentWaitSubmissions.empty();
	const bool requiresDeviceIdle = commandRingFaulted || submissionQueueLockUncertain ||
		hasRegisteredPresentWaits ||
		std::find(presentWaitInUse.begin(), presentWaitInUse.end(), true) != presentWaitInUse.end();
	if (requiresDeviceIdle && !WaitDeviceIdle()) {
		logger::error("[DXVKInterop] command resources remain quarantined because device idle could not be proven");
		return;
	}
	if (hasRegisteredPresentWaits && !ClearReleasedPresentWaitsAfterIdle()) {
		logger::error("[DXVKInterop] command resources remain quarantined until all present waits are idle-released");
		return;
	}

	if (!requiresDeviceIdle) {
		for (auto f : commandFences) {
			if (f == VK_NULL_HANDLE)
				continue;
			const VulkanResultAttempt waitAttempt = WaitForFenceSEH(device, f);
			if (waitAttempt.result != VK_SUCCESS) {
				commandRingFaulted = true;
				if (waitAttempt.exceptionCode) {
					logger::error("[DXVKInterop] command-resource fence wait faulted (SEH {:#x})",
						waitAttempt.exceptionCode);
				} else {
					logger::error("[DXVKInterop] command-resource fence wait failed ({})",
						static_cast<int>(waitAttempt.result));
				}
				return;
			}
		}
	}
	commandRingSubmissionsIdleProven = true;
	if (!vkDestroyImageView) {
		for (const auto& slot : pendingViewDeletes) {
			if (std::find_if(slot.begin(), slot.end(),
					[](VkImageView a_view) { return a_view != VK_NULL_HANDLE; }) != slot.end()) {
				commandRingFaulted = true;
				logger::error("[DXVKInterop] vkDestroyImageView is unavailable; command resources remain quarantined");
				return;
			}
		}
	} else {
		for (size_t slotIndex = 0; slotIndex < pendingViewDeletes.size(); ++slotIndex) {
			auto& slot = pendingViewDeletes[slotIndex];
			for (VkImageView& v : slot) {
				if (v == VK_NULL_HANDLE)
					continue;
				const VulkanVoidAttempt destroyAttempt = DestroyImageViewSEH(vkDestroyImageView, device, v);
				if (!destroyAttempt.completed) {
					commandRingFaulted = true;
					vulkanResourceDestructionTerminalFault = true;
					v = VK_NULL_HANDLE;
					logger::error("[DXVKInterop] image-view destruction faulted (SEH {:#x}); handle poisoned and backing slot quarantined",
						destroyAttempt.exceptionCode);
					return;
				}
				v = VK_NULL_HANDLE;
			}
			slot.clear();
			if (slotIndex < pendingResourceReleases.size())
				pendingResourceReleases[slotIndex].clear();
		}
	}
	pendingViewDeletes.clear();
	pendingResourceReleases.clear();
	ReleaseRetainedFSRResourcesIfSafe();
	for (VkSemaphore& semaphore : presentWaitSemaphores) {
		if (semaphore == VK_NULL_HANDLE)
			continue;
		const VulkanVoidAttempt destroyAttempt = DestroySemaphoreSEH(device, semaphore);
		if (!destroyAttempt.completed) {
			commandRingFaulted = true;
			vulkanResourceDestructionTerminalFault = true;
			semaphore = VK_NULL_HANDLE;
			logger::error("[DXVKInterop] semaphore destruction faulted (SEH {:#x}); handle poisoned",
				destroyAttempt.exceptionCode);
			return;
		}
		semaphore = VK_NULL_HANDLE;
	}
	presentWaitSemaphores.clear();
	presentWaitInUse.clear();
	pushedPresentWaitSlot = UINT32_MAX;
	pushedPresentWaitGeneration = 0;
	for (VkFence& f : commandFences) {
		if (f == VK_NULL_HANDLE)
			continue;
		if (!DestroyFenceSEH(device, f)) {
			commandRingFaulted = true;
			vulkanResourceDestructionTerminalFault = true;
			f = VK_NULL_HANDLE;
			logger::error("[DXVKInterop] fence destruction faulted; handle poisoned");
			return;
		}
		f = VK_NULL_HANDLE;
	}
	commandFences.clear();

	if (commandPool != VK_NULL_HANDLE) {
		const VulkanVoidAttempt destroyAttempt = DestroyCommandPoolSEH(device, commandPool);
		if (!destroyAttempt.completed) {
			commandRingFaulted = true;
			vulkanResourceDestructionTerminalFault = true;
			commandPool = VK_NULL_HANDLE;
			logger::error("[DXVKInterop] command-pool destruction faulted (SEH {:#x}); handle poisoned",
				destroyAttempt.exceptionCode);
			return;
		}
		commandPool = VK_NULL_HANDLE;
	}
	commandBuffers.clear();
	framesInFlight = 0;
	commandFrameIndex = 0;
	commandRingFaulted = false;
}

bool DXVKInterop::DrainCommandRing()
{
	std::lock_guard lock(commandRingMutex);
	if (vulkanResourceDestructionTerminalFault)
		return false;
	if (submissionQueueLockUncertain)
		return false;
	if (commandPool == VK_NULL_HANDLE)
		return true;
	if (device == VK_NULL_HANDLE)
		return false;
	const bool hasRegisteredPresentWaits =
		pushedPresentWaitSlot != UINT32_MAX || !outstandingPresentWaitSubmissions.empty();
	const bool requiresDeviceIdle = commandRingFaulted || hasRegisteredPresentWaits;
	if (requiresDeviceIdle && !WaitDeviceIdle()) {
		logger::error("[DXVKInterop] command-ring resources remain quarantined because device idle could not be proven");
		return false;
	}
	if (hasRegisteredPresentWaits && !ClearReleasedPresentWaitsAfterIdle()) {
		logger::error("[DXVKInterop] command-ring resources remain quarantined until all present waits are idle-released");
		return false;
	}

	// Leave fences signaled; BeginFrameCommandBuffer resets them on reuse.
	if (!requiresDeviceIdle) {
		for (VkFence fence : commandFences) {
			if (fence == VK_NULL_HANDLE)
				continue;
			const VulkanResultAttempt waitAttempt = WaitForFenceSEH(device, fence);
			if (waitAttempt.result != VK_SUCCESS) {
				commandRingFaulted = true;
				if (waitAttempt.exceptionCode) {
					logger::error("[DXVKInterop] command-ring fence wait faulted (SEH {:#x})",
						waitAttempt.exceptionCode);
				} else {
					logger::error("[DXVKInterop] failed to drain a command-ring fence ({})",
						static_cast<int>(waitAttempt.result));
				}
				return false;
			}
		}
	}
	commandRingSubmissionsIdleProven = true;

	if (!vkDestroyImageView) {
		for (const auto& slot : pendingViewDeletes) {
			if (std::find_if(slot.begin(), slot.end(),
					[](VkImageView a_view) { return a_view != VK_NULL_HANDLE; }) != slot.end()) {
				commandRingFaulted = true;
				logger::error("[DXVKInterop] vkDestroyImageView is unavailable; deferred views remain quarantined");
				return false;
			}
		}
	} else {
		for (size_t slotIndex = 0; slotIndex < pendingViewDeletes.size(); ++slotIndex) {
			auto& slot = pendingViewDeletes[slotIndex];
			for (VkImageView& v : slot) {
				if (v == VK_NULL_HANDLE)
					continue;
				const VulkanVoidAttempt destroyAttempt = DestroyImageViewSEH(vkDestroyImageView, device, v);
				if (!destroyAttempt.completed) {
					commandRingFaulted = true;
					vulkanResourceDestructionTerminalFault = true;
					v = VK_NULL_HANDLE;
					logger::error("[DXVKInterop] deferred image-view destruction faulted (SEH {:#x}); handle poisoned and backing slot quarantined",
						destroyAttempt.exceptionCode);
					return false;
				}
				v = VK_NULL_HANDLE;
			}
			slot.clear();
			if (slotIndex < pendingResourceReleases.size())
				pendingResourceReleases[slotIndex].clear();
		}
	}
	ReleaseRetainedFSRResourcesIfSafe();
	return true;
}

bool DXVKInterop::CommandResourcesReady() const
{
	std::lock_guard lock(commandRingMutex);
	return commandPool != VK_NULL_HANDLE && !commandRingFaulted &&
	       !vulkanResourceDestructionTerminalFault && !submissionQueueLockUncertain;
}

bool DXVKInterop::HasCommandRingFault() const
{
	std::lock_guard lock(commandRingMutex);
	return commandRingFaulted || vulkanResourceDestructionTerminalFault || submissionQueueLockUncertain;
}

bool DXVKInterop::IsDeviceLost() const
{
	std::lock_guard lock(commandRingMutex);
	return deviceLost;
}

bool DXVKInterop::IsPresentWaitUnattachedForSwapchain() const
{
	std::lock_guard lock(commandRingMutex);
	return presentWaitUnattachedForSwapchain;
}

void DXVKInterop::ResetPresentWaitUnattachedForSwapchain()
{
	std::lock_guard lock(commandRingMutex);
	presentWaitUnattachedForSwapchain = false;
}

bool DXVKInterop::RecoverCommandRing()
{
	std::lock_guard lock(commandRingMutex);
	if (vulkanResourceDestructionTerminalFault)
		return false;
	if (!commandRingFaulted)
		return commandPool != VK_NULL_HANDLE && !submissionQueueLockUncertain;
	if (submissionQueueLockUncertain) {
		logger::error("[DXVKInterop] command ring cannot be recovered because the submission queue lock state is uncertain");
		return false;
	}

	const uint32_t ringDepth = framesInFlight ? framesInFlight : 3;
	if (!WaitDeviceIdle())
		return false;
	DestroyCommandResources();
	if (commandPool != VK_NULL_HANDLE)
		return false;
	if (!CreateCommandResources(ringDepth)) {
		logger::error("[DXVKInterop] failed to recreate the quarantined command ring");
		return false;
	}

	logger::warn("[DXVKInterop] recovered the command ring after an ambiguous submission fault");
	return true;
}

bool DXVKInterop::PresentWaitInteropReady() const
{
	std::lock_guard lock(commandRingMutex);
	return enqueueInteropCommandBuffer != nullptr && getPresentWaitSemaphoreState != nullptr &&
	       clearPresentWaitSemaphore != nullptr && cancelPresentWaitSemaphore != nullptr &&
	       releaseQueuedPresentWaitSemaphoresAfterIdle != nullptr &&
	       !presentWaitInteropTerminalFault && synchronousPresentControlAvailable &&
	       !presentQueueSplit && !submissionQueueLockUncertain;
}

bool DXVKInterop::FrameGenerationQueueInteropReady() const
{
	std::lock_guard lock(commandRingMutex);
	return available && !presentQueueSplit && !submissionQueueLockUncertain;
}

DXVKInterop::CommandTransaction DXVKInterop::BeginFrameCommandBuffer()
{
	std::unique_lock ringLock(commandRingMutex);
	if (commandPool == VK_NULL_HANDLE || commandRingFaulted || submissionQueueLockUncertain)
		return {};

	// Avoid waiting while Streamline owns the presenting queue; grow the ring if needed.
	constexpr uint32_t kMaxRingDepth = 64;
	uint32_t next = (commandFrameIndex + 1) % framesInFlight;
	VulkanResultAttempt nextFenceAttempt = GetFenceStatusSEH(device, commandFences[next]);
	if (nextFenceAttempt.exceptionCode ||
		(nextFenceAttempt.result != VK_SUCCESS && nextFenceAttempt.result != VK_NOT_READY)) {
		commandRingFaulted = true;
		if (nextFenceAttempt.exceptionCode) {
			logger::error("[DXVKInterop] vkGetFenceStatus faulted (SEH {:#x})",
				nextFenceAttempt.exceptionCode);
		} else {
			logger::error("[DXVKInterop] vkGetFenceStatus failed ({})",
				static_cast<int>(nextFenceAttempt.result));
		}
		return {};
	}
	if (presentWaitInUse[next] || nextFenceAttempt.result == VK_NOT_READY) {
		uint32_t freeSlot = UINT32_MAX;
		for (uint32_t i = 0; i < framesInFlight; ++i) {
			const uint32_t cand = (next + i) % framesInFlight;
			if (presentWaitInUse[cand])
				continue;
			const VulkanResultAttempt candidateAttempt = GetFenceStatusSEH(device, commandFences[cand]);
			if (candidateAttempt.exceptionCode ||
				(candidateAttempt.result != VK_SUCCESS && candidateAttempt.result != VK_NOT_READY)) {
				commandRingFaulted = true;
				if (candidateAttempt.exceptionCode) {
					logger::error("[DXVKInterop] vkGetFenceStatus faulted (SEH {:#x})",
						candidateAttempt.exceptionCode);
				} else {
					logger::error("[DXVKInterop] vkGetFenceStatus failed ({})",
						static_cast<int>(candidateAttempt.result));
				}
				return {};
			}
			if (candidateAttempt.result == VK_SUCCESS) {
				freeSlot = cand;
				break;
			}
		}
		if (freeSlot != UINT32_MAX) {
			next = freeSlot;
		} else if (framesInFlight < kMaxRingDepth) {
			VkCommandBuffer newCb = VK_NULL_HANDLE;
			VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocInfo.commandPool = commandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;
			VkFence newFence = VK_NULL_HANDLE;
			VkSemaphore newSemaphore = VK_NULL_HANDLE;
			VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			const VulkanResultAttempt allocateAttempt = AllocateCommandBuffersSEH(device, &allocInfo, &newCb);
			const bool commandBufferAllocated =
				!allocateAttempt.exceptionCode && allocateAttempt.result == VK_SUCCESS;
			if (!commandBufferAllocated)
				newCb = VK_NULL_HANDLE;
			DWORD fenceExceptionCode = 0;
			const VkResult fenceResult = commandBufferAllocated ?
				CreateSignaledFenceSEH(device, &newFence, &fenceExceptionCode) : allocateAttempt.result;
			const bool fenceCreated = commandBufferAllocated && !fenceExceptionCode && fenceResult == VK_SUCCESS;
			if (!fenceCreated)
				newFence = VK_NULL_HANDLE;
			VulkanResultAttempt semaphoreAttempt{};
			semaphoreAttempt.result = fenceResult;
			bool semaphoreCreated = false;
			if (fenceCreated) {
				semaphoreAttempt.result = VK_SUCCESS;
				if (PresentWaitInteropReady()) {
					semaphoreAttempt = CreateSemaphoreSEH(device, &semaphoreInfo, &newSemaphore);
					semaphoreCreated =
						!semaphoreAttempt.exceptionCode && semaphoreAttempt.result == VK_SUCCESS;
					if (!semaphoreCreated)
						newSemaphore = VK_NULL_HANDLE;
				}
			}
			if (commandBufferAllocated && fenceCreated && semaphoreAttempt.result == VK_SUCCESS &&
				!semaphoreAttempt.exceptionCode) {
				commandBuffers.push_back(newCb);
				commandFences.push_back(newFence);
				presentWaitSemaphores.push_back(newSemaphore);
				presentWaitInUse.push_back(false);
				pendingViewDeletes.emplace_back();
				pendingResourceReleases.emplace_back();
				next = framesInFlight;
				++framesInFlight;
				logger::info("[DXVKInterop] Command ring grown to {} (all slots in flight)", framesInFlight);
			} else {
				const DWORD creationExceptionCode = allocateAttempt.exceptionCode ? allocateAttempt.exceptionCode :
				                                    fenceExceptionCode ? fenceExceptionCode :
				                                    semaphoreAttempt.exceptionCode;
				commandRingFaulted = true;
				if (creationExceptionCode) {
					vulkanResourceDestructionTerminalFault = true;
					newCb = VK_NULL_HANDLE;
					newFence = VK_NULL_HANDLE;
					newSemaphore = VK_NULL_HANDLE;
					logger::error("[DXVKInterop] command ring growth faulted (SEH {:#x}); created resources quarantined",
						creationExceptionCode);
					return {};
				}

				VulkanVoidAttempt semaphoreDestroyAttempt{};
				semaphoreDestroyAttempt.completed = !semaphoreCreated;
				if (semaphoreCreated)
					semaphoreDestroyAttempt = DestroySemaphoreSEH(device, newSemaphore);
				bool fenceDestroyed = !fenceCreated;
				VulkanVoidAttempt bufferFreeAttempt{};
				bufferFreeAttempt.completed = !commandBufferAllocated;
				if (semaphoreDestroyAttempt.completed && fenceCreated) {
					fenceDestroyed = DestroyFenceSEH(device, newFence);
				}
				if (semaphoreDestroyAttempt.completed && fenceDestroyed && commandBufferAllocated) {
					bufferFreeAttempt = FreeCommandBuffersSEH(
						device, commandPool, 1u, &newCb);
				}
				if (semaphoreCreated && semaphoreDestroyAttempt.completed)
					newSemaphore = VK_NULL_HANDLE;
				else if (!semaphoreDestroyAttempt.completed) {
					newSemaphore = VK_NULL_HANDLE;
					vulkanResourceDestructionTerminalFault = true;
				}
				if (fenceCreated && fenceDestroyed)
					newFence = VK_NULL_HANDLE;
				else if (!fenceDestroyed) {
					newFence = VK_NULL_HANDLE;
					vulkanResourceDestructionTerminalFault = true;
				}
				if (commandBufferAllocated && bufferFreeAttempt.completed)
					newCb = VK_NULL_HANDLE;
				else if (!bufferFreeAttempt.completed) {
					newCb = VK_NULL_HANDLE;
					vulkanResourceDestructionTerminalFault = true;
				}
				const DWORD exceptionCode = !semaphoreDestroyAttempt.completed ? semaphoreDestroyAttempt.exceptionCode :
				                            !fenceDestroyed ? ERROR_UNHANDLED_EXCEPTION :
				                            !bufferFreeAttempt.completed ? bufferFreeAttempt.exceptionCode : 0;
				if (exceptionCode) {
					logger::error("[DXVKInterop] command ring growth faulted (SEH {:#x})", exceptionCode);
				} else {
					logger::error("[DXVKInterop] command ring growth failed (allocate={}, fence={}, semaphore={})",
						static_cast<int>(allocateAttempt.result), static_cast<int>(fenceResult),
						static_cast<int>(semaphoreAttempt.result));
				}
				return {};
			}
		} else {
			static bool s_warned = false;
			if (!s_warned) {
				s_warned = true;
				logger::error("[DXVKInterop] command ring reached max depth {} - refusing to block the render thread", kMaxRingDepth);
			}
			return {};
		}
	}
	commandFrameIndex = next;
	VkCommandBuffer cb = commandBuffers[commandFrameIndex];

	// Reusing a signaled slot makes its deferred views safe to destroy.
	if (commandFrameIndex < pendingViewDeletes.size()) {
		auto& dead = pendingViewDeletes[commandFrameIndex];
		if (vkDestroyImageView) {
			for (VkImageView& v : dead) {
				if (v == VK_NULL_HANDLE)
					continue;
				const VulkanVoidAttempt destroyAttempt = DestroyImageViewSEH(vkDestroyImageView, device, v);
				if (!destroyAttempt.completed) {
					commandRingFaulted = true;
					vulkanResourceDestructionTerminalFault = true;
					v = VK_NULL_HANDLE;
					logger::error("[DXVKInterop] deferred image-view destruction faulted (SEH {:#x})",
						destroyAttempt.exceptionCode);
					return {};
				}
				v = VK_NULL_HANDLE;
			}
		} else if (std::find_if(dead.begin(), dead.end(),
				       [](VkImageView a_view) { return a_view != VK_NULL_HANDLE; }) != dead.end()) {
			commandRingFaulted = true;
			logger::error("[DXVKInterop] vkDestroyImageView is unavailable; deferred views remain quarantined");
			return {};
		}
		dead.clear();
	}
	if (commandFrameIndex < pendingResourceReleases.size())
		pendingResourceReleases[commandFrameIndex].clear();
	ReleaseRetainedFSRResourcesIfSafe();

	const VulkanResultAttempt resetAttempt = ResetCommandBufferSEH(cb);
	if (resetAttempt.result != VK_SUCCESS) {
		commandRingFaulted = true;
		if (resetAttempt.exceptionCode) {
			logger::error("[DXVKInterop] vkResetCommandBuffer faulted (SEH {:#x})",
				resetAttempt.exceptionCode);
		} else {
			logger::error("[DXVKInterop] vkResetCommandBuffer failed ({})",
				static_cast<int>(resetAttempt.result));
		}
		return {};
	}

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	const VulkanResultAttempt beginAttempt = BeginCommandBufferSEH(cb, &beginInfo);
	if (beginAttempt.result != VK_SUCCESS) {
		commandRingFaulted = true;
		if (beginAttempt.exceptionCode) {
			logger::error("[DXVKInterop] vkBeginCommandBuffer faulted (SEH {:#x})",
				beginAttempt.exceptionCode);
		} else {
			logger::error("[DXVKInterop] vkBeginCommandBuffer failed ({})",
				static_cast<int>(beginAttempt.result));
		}
		return {};
	}
	return CommandTransaction(this, commandFrameIndex, cb, std::move(ringLock));
}

bool DXVKInterop::SubmitFrameCommandBuffer(CommandTransaction& a_transaction,
	bool a_signalForNextPresent)
{
	if (a_transaction.owner != this || !a_transaction.ringLock.owns_lock() ||
		a_transaction.submitted || a_transaction.slot >= commandBuffers.size() ||
		a_transaction.commandBuffer == VK_NULL_HANDLE ||
		commandBuffers[a_transaction.slot] != a_transaction.commandBuffer ||
		commandPool == VK_NULL_HANDLE || commandRingFaulted || submissionQueueLockUncertain)
		return false;
	const uint32_t slot = a_transaction.slot;
	const VkCommandBuffer commandBuffer = a_transaction.commandBuffer;
	if (a_signalForNextPresent &&
		(!PresentWaitInteropReady() ||
		 pushedPresentWaitSlot != UINT32_MAX || slot >= presentWaitSemaphores.size() ||
		 presentWaitSemaphores[slot] == VK_NULL_HANDLE || presentWaitInUse[slot])) {
		logger::error("[DXVKInterop] no safe semaphore slot is available for the next present");
		return false;
	}

	VkFence& fence = commandFences[slot];
	VkSemaphore signalSemaphore = VK_NULL_HANDLE;
	if (a_signalForNextPresent)
		signalSemaphore = presentWaitSemaphores[slot];

	commandRingSubmissionsIdleProven = false;
	const QueueSubmitAttempt attempt = EnqueueInteropSEH(
		interopDevice.get(), device, commandBuffer, fence, signalSemaphore,
		enqueueInteropCommandBuffer);
	// DXVK owns the queue lock now, so there is no foreign-submission lock state to
	// go uncertain here; the flag stays for the device-idle path that still takes it.
	// A SEH fault is the only genuinely ambiguous outcome: it can land after DXVK has
	// already taken ownership of the command buffer. A clean non-success return means
	// DXVK refused the submission outright and nothing is in flight.
	if (attempt.faulted || attempt.endResult != VK_SUCCESS ||
		attempt.resetResult != VK_SUCCESS || attempt.submitResult != VK_SUCCESS) {
		commandRingFaulted = true;
		if (attempt.faulted) {
			logger::error("[DXVKInterop] dxvkEnqueueInteropCommandBuffer faulted (SEH {:#x})",
				attempt.exceptionCode);
			// DXVK may already own the command buffer. Keep the fence, command buffer,
			// semaphore and every attached resource quarantined until a real device-idle
			// succeeds; a synthetic signaled fence cannot prove GPU completion.
			a_transaction.submissionMayBeInFlight = true;
			if (a_signalForNextPresent)
				presentWaitInUse[slot] = true;
			return false;
		} else if (attempt.endResult != VK_SUCCESS) {
			logger::error("[DXVKInterop] vkEndCommandBuffer failed ({})",
				static_cast<int>(attempt.endResult));
		} else if (attempt.resetResult != VK_SUCCESS) {
			logger::error("[DXVKInterop] vkResetFences failed ({})",
				static_cast<int>(attempt.resetResult));
		} else {
			logger::error("[DXVKInterop] DXVK rejected the interop command buffer");
		}
		return false;
	}
	a_transaction.submitted = true;

	if (a_signalForNextPresent) {
		presentWaitInUse[slot] = true;
		// The semaphore was registered by the same call that submitted it, so there is
		// no separate push step and no window where the submit is live but unregistered.
		pushedPresentWaitSlot = slot;
		pushedPresentWaitGeneration = attempt.generation;
	}

	return true;
}

bool DXVKInterop::PushPendingPresentWaitSemaphore()
{
	std::lock_guard lock(commandRingMutex);
	// Registration is no longer a separate step: dxvkEnqueueInteropCommandBuffer submits
	// the command buffer and registers its signal semaphore in one call, so by the time
	// SubmitFrameCommandBuffer returns the generation is already keyed. This remains so
	// callers that ran the old submit-then-push sequence stay correct; it succeeds when a
	// registered generation is present and reports failure only when there genuinely is
	// nothing registered.
	if (presentWaitInteropTerminalFault || commandRingFaulted)
		return false;
	if (pushedPresentWaitSlot != UINT32_MAX && pushedPresentWaitGeneration)
		return true;
	return false;
}

bool DXVKInterop::HasPendingPresentWaitSemaphore() const
{
	std::lock_guard lock(commandRingMutex);
	// dxvkEnqueueInteropCommandBuffer registers the semaphore as part of the submit, so a
	// registered-but-not-yet-presented generation IS the pending state. Reporting it here is
	// what lets the fault-teardown path discard it instead of leaving it unpresented.
	return pushedPresentWaitSlot != UINT32_MAX ||
	       (presentWaitInteropTerminalFault && !outstandingPresentWaitSubmissions.empty());
}

bool DXVKInterop::DiscardPendingPresentWaitSemaphore()
{
	std::lock_guard lock(commandRingMutex);
	if (presentWaitInteropTerminalFault)
		return false;
	if (pushedPresentWaitSlot == UINT32_MAX)
		return true;

	const uint32_t slot = pushedPresentWaitSlot;
	if (slot < presentWaitSemaphores.size() &&
		presentWaitSemaphores[slot] != VK_NULL_HANDLE && cancelPresentWaitSemaphore) {
		const PresentWaitStateAttempt cancelAttempt =
			CancelPresentWaitSemaphoreSEH(cancelPresentWaitSemaphore, presentWaitSemaphores[slot]);
		if (!cancelAttempt.exceptionCode && cancelAttempt.state) {
			pushedPresentWaitSlot = UINT32_MAX;
			pushedPresentWaitGeneration = 0;

			commandRingFaulted = true;
			logger::warn("[DXVKInterop] quarantined a cancelled present-wait semaphore for command-ring recovery");
			return true;
		}
	}
	if (!WaitDeviceIdle())
		return false;
	if (clearPresentWaitSemaphore && pushedPresentWaitGeneration) {
		const PresentWaitStateAttempt clearAttempt =
			ClearPresentWaitSemaphoreSEH(clearPresentWaitSemaphore, pushedPresentWaitGeneration);
		if (clearAttempt.exceptionCode || !clearAttempt.state)
			logger::warn("[DXVKInterop] idle-released present-wait generation could not be cleared after device idle");
	}
	pushedPresentWaitSlot = UINT32_MAX;
	pushedPresentWaitGeneration = 0;
	commandRingFaulted = true;
	logger::warn("[DXVKInterop] quarantined an attached present-wait semaphore for command-ring recovery after device idle");
	return true;
}

void DXVKInterop::NotifyPresentWaitQueued()
{
	std::lock_guard lock(commandRingMutex);
	if (presentWaitInteropTerminalFault)
		return;
	constexpr uint32_t kPresentWaitPending = 1;
	constexpr uint32_t kPresentWaitQueued = 2;
	constexpr uint32_t kPresentWaitUncertain = 3;
	constexpr uint32_t kPresentWaitReleased = 4;
	const auto latchTerminalFault = [&](const char* a_operation, DWORD a_exceptionCode = 0) {
		commandRingFaulted = true;
		presentWaitInteropTerminalFault = true;
		enqueueInteropCommandBuffer = nullptr;
		pushedPresentWaitSlot = UINT32_MAX;
		pushedPresentWaitGeneration = 0;
		outstandingPresentWaitSubmissions.clear();
		if (a_exceptionCode) {
			logger::critical("[DXVKInterop] {} faulted (SEH {:#x}); present-wait interop disabled",
				a_operation, a_exceptionCode);
		} else {
			logger::critical("[DXVKInterop] {}; present-wait interop disabled", a_operation);
		}
	};

	for (size_t i = 0; i < outstandingPresentWaitSubmissions.size();) {
		const PresentWaitSubmission submission = outstandingPresentWaitSubmissions[i];
		const PresentWaitStateAttempt stateAttempt = GetPresentWaitSemaphoreStateSEH(
			getPresentWaitSemaphoreState, submission.generation);
		if (stateAttempt.exceptionCode) {
			latchTerminalFault("present-wait release query", stateAttempt.exceptionCode);
			return;
		}
		if (stateAttempt.state == kPresentWaitQueued) {
			++i;
			continue;
		}
		if (stateAttempt.state != kPresentWaitReleased || submission.slot >= presentWaitInUse.size()) {
			latchTerminalFault(stateAttempt.state == kPresentWaitUncertain ?
				"present-wait consumption is uncertain" : "present-wait release state is invalid");
			return;
		}
		const PresentWaitStateAttempt clearAttempt = ClearPresentWaitSemaphoreSEH(
			clearPresentWaitSemaphore, submission.generation);
		if (clearAttempt.exceptionCode || !clearAttempt.state) {
			latchTerminalFault("released present-wait generation could not be cleared",
				clearAttempt.exceptionCode);
			return;
		}
		presentWaitInUse[submission.slot] = false;
		outstandingPresentWaitSubmissions.erase(outstandingPresentWaitSubmissions.begin() + i);
	}

	if (pushedPresentWaitSlot == UINT32_MAX)
		return;
	const PresentWaitStateAttempt stateAttempt = GetPresentWaitSemaphoreStateSEH(
		getPresentWaitSemaphoreState, pushedPresentWaitGeneration);
	if (stateAttempt.exceptionCode) {
		latchTerminalFault("present-wait state query", stateAttempt.exceptionCode);
		return;
	}
	if (stateAttempt.state == kPresentWaitQueued) {
		outstandingPresentWaitSubmissions.push_back(
			PresentWaitSubmission{ pushedPresentWaitSlot, pushedPresentWaitGeneration });
		pushedPresentWaitSlot = UINT32_MAX;
		pushedPresentWaitGeneration = 0;
		return;
	}
	if (stateAttempt.state == kPresentWaitReleased) {
		if (pushedPresentWaitSlot >= presentWaitInUse.size()) {
			latchTerminalFault("released present-wait slot is invalid");
			return;
		}
		const PresentWaitStateAttempt clearAttempt = ClearPresentWaitSemaphoreSEH(
			clearPresentWaitSemaphore, pushedPresentWaitGeneration);
		if (clearAttempt.exceptionCode || !clearAttempt.state) {
			latchTerminalFault("released present-wait generation could not be cleared",
				clearAttempt.exceptionCode);
			return;
		}
		presentWaitInUse[pushedPresentWaitSlot] = false;
		pushedPresentWaitSlot = UINT32_MAX;
		pushedPresentWaitGeneration = 0;
		return;
	}
	if (stateAttempt.state == kPresentWaitPending) {
		if (pushedPresentWaitSlot >= presentWaitSemaphores.size()) {
			latchTerminalFault("pending present-wait slot is invalid");
			return;
		}
		const PresentWaitStateAttempt cancelAttempt = CancelPresentWaitSemaphoreSEH(
			cancelPresentWaitSemaphore, presentWaitSemaphores[pushedPresentWaitSlot]);
		if (cancelAttempt.exceptionCode || !cancelAttempt.state) {
			latchTerminalFault("pending present-wait registration could not be cancelled",
				cancelAttempt.exceptionCode);
			return;
		}
		const uint32_t slot = pushedPresentWaitSlot;
		pushedPresentWaitSlot = UINT32_MAX;
		pushedPresentWaitGeneration = 0;
		if (!WaitDeviceIdle()) {
			commandRingFaulted = true;
			logger::error("[DXVKInterop] cancelled present-wait semaphore remains quarantined because device idle could not be proven");
			return;
		}
		// The registration is cancelled and the device is provably idle, but the submit that
		// carried this one-shot semaphore may already have signalled it, and no present will
		// ever wait on it now. A signalled binary semaphore cannot legally be re-signalled, so
		// the slot's semaphore has to be replaced before the slot is reused. Replacing just this
		// semaphore is sufficient; previously this path faulted the whole ring instead, which
		// destroyed and recreated every command buffer, fence and semaphore once per unconsumed
		// present. Under a present stall that repeated every frame - thousands of times a
		// session - and kept frame generation from ever engaging while the game kept
		// rendering, so it also read as a false pass in the freeze harness.
		VkSemaphore& stale = presentWaitSemaphores[slot];
		const VulkanVoidAttempt destroyAttempt = DestroySemaphoreSEH(device, stale);
		if (!destroyAttempt.completed) {
			stale = VK_NULL_HANDLE;
			vulkanResourceDestructionTerminalFault = true;
			commandRingFaulted = true;
			logger::error("[DXVKInterop] stale present-wait semaphore destruction faulted (SEH {:#x})",
				destroyAttempt.exceptionCode);
			return;
		}
		stale = VK_NULL_HANDLE;
		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		const VulkanResultAttempt createAttempt = CreateSemaphoreSEH(device, &semaphoreInfo, &stale);
		if (createAttempt.exceptionCode || createAttempt.result != VK_SUCCESS) {
			stale = VK_NULL_HANDLE;
			commandRingFaulted = true;
			if (createAttempt.exceptionCode) {
				logger::error("[DXVKInterop] present-wait semaphore recreation faulted (SEH {:#x})",
					createAttempt.exceptionCode);
			} else {
				logger::error("[DXVKInterop] present-wait semaphore recreation failed ({})",
					static_cast<int>(createAttempt.result));
			}
			return;
		}
		presentWaitInUse[slot] = false;
		// A present went by without taking this semaphore, so the presenter is not attaching
		// present-wait semaphores to this swapchain. Stop asking for one until it is recreated;
		// otherwise every subsequent present orphans another.
		if (!std::exchange(presentWaitUnattachedForSwapchain, true))
			logger::warn("[DXVKInterop] replaced an unpresented one-shot semaphore (slot {}); presenter is not attaching present-wait semaphores to this swapchain, suppressing until recreate", slot);
		return;
	}
	latchTerminalFault(stateAttempt.state == kPresentWaitUncertain ?
		"present-wait consumption is uncertain" : "present-wait state is invalid");
}

void DXVKInterop::QueueViewsForDeferredDelete(const CommandTransaction& a_transaction,
	const VkImageView* a_views, uint32_t a_count)
{
	if (a_transaction.owner != this || !a_transaction.ringLock.owns_lock() ||
		(!a_transaction.submitted && !a_transaction.submissionMayBeInFlight) ||
		!a_views || a_transaction.slot >= pendingViewDeletes.size())
		return;
	auto& slot = pendingViewDeletes[a_transaction.slot];
	for (uint32_t i = 0; i < a_count; ++i)
		if (a_views[i] != VK_NULL_HANDLE)
			slot.push_back(a_views[i]);
}

void DXVKInterop::QueueResourcesForDeferredRelease(const CommandTransaction& a_transaction,
	ID3D11Resource* const* a_resources, uint32_t a_count)
{
	if (a_transaction.owner != this || !a_transaction.ringLock.owns_lock() ||
		(!a_transaction.submitted && !a_transaction.submissionMayBeInFlight) ||
		!a_resources || a_transaction.slot >= pendingResourceReleases.size())
		return;
	auto& slot = pendingResourceReleases[a_transaction.slot];
	for (uint32_t i = 0; i < a_count; ++i) {
		if (a_resources[i] == nullptr)
			continue;
		winrt::com_ptr<ID3D11Resource> resource;
		resource.copy_from(a_resources[i]);
		slot.push_back(std::move(resource));
	}
}

void DXVKInterop::QueueResourcesForPresent(const CommandTransaction& a_transaction,
	ID3D11Resource* const* a_resources, uint32_t a_count)
{
	if (a_transaction.owner != this || !a_transaction.ringLock.owns_lock() ||
		!a_resources)
		return;
	fsrSwapchainTeardownConfirmed = false;
	for (uint32_t i = 0; i < a_count; ++i) {
		ID3D11Resource* resource = a_resources[i];
		if (!resource)
			continue;
		const auto duplicate = std::find_if(
			retainedPresentResources.begin(), retainedPresentResources.end(),
			[resource](const auto& a_held) { return a_held.get() == resource; });
		if (duplicate != retainedPresentResources.end())
			continue;
		winrt::com_ptr<ID3D11Resource> heldResource;
		heldResource.copy_from(resource);
		retainedPresentResources.push_back(std::move(heldResource));
	}
}

void DXVKInterop::QuarantineResourcesAfterVulkanDestructionFault(
	ID3D11Resource* const* a_resources, uint32_t a_count)
{
	std::lock_guard lock(commandRingMutex);
	commandRingFaulted = true;
	vulkanResourceDestructionTerminalFault = true;
	if (!a_resources)
		return;
	for (uint32_t i = 0; i < a_count; ++i) {
		ID3D11Resource* resource = a_resources[i];
		if (!resource)
			continue;
		const auto duplicate = std::find_if(
			retainedPresentResources.begin(), retainedPresentResources.end(),
			[resource](const auto& a_held) { return a_held.get() == resource; });
		if (duplicate != retainedPresentResources.end())
			continue;
		winrt::com_ptr<ID3D11Resource> heldResource;
		heldResource.copy_from(resource);
		retainedPresentResources.push_back(std::move(heldResource));
	}
}

void DXVKInterop::ReleaseRetainedFSRResourcesIfSafe()
{
	if (vulkanResourceDestructionTerminalFault || !fsrSwapchainTeardownConfirmed ||
		!pendingFSRPresentViewGroups.empty())
		return;

	if (!quarantinedFSRPresentViewGroups.empty() && commandRingSubmissionsIdleProven && vkDestroyImageView) {
		for (auto& group : quarantinedFSRPresentViewGroups) {
			for (VkImageView& view : group.views) {
				if (view == VK_NULL_HANDLE)
					continue;
				const VulkanVoidAttempt destroyAttempt = DestroyImageViewSEH(vkDestroyImageView, device, view);
				if (!destroyAttempt.completed) {
					vulkanResourceDestructionTerminalFault = true;
					commandRingFaulted = true;
					view = VK_NULL_HANDLE;
					logger::error("[DXVKInterop] quarantined FSR view destruction faulted (SEH {:#x})",
						destroyAttempt.exceptionCode);
					return;
				}
				view = VK_NULL_HANDLE;
			}
		}
		quarantinedFSRPresentViewGroups.clear();
	}

	if (!quarantinedFSRPresentViewGroups.empty())
		return;

	const bool viewsPending = std::any_of(pendingViewDeletes.begin(), pendingViewDeletes.end(),
		[](const auto& a_slot) { return !a_slot.empty(); });
	if (!viewsPending && !retainedPresentResources.empty()) {
		logger::debug("[DXVKInterop] releasing {} resources after completed FSR teardown and view retirement",
			retainedPresentResources.size());
		retainedPresentResources.clear();
	}
}

void DXVKInterop::QueueViewsForFSRPresent(const CommandTransaction& a_transaction,
	const VkImageView* a_views, uint32_t a_count)
{
	if (a_transaction.owner != this || !a_transaction.ringLock.owns_lock() ||
		!a_views || a_transaction.slot >= pendingViewDeletes.size())
		return;
	FSRPresentViewGroup group{};
	group.slot = a_transaction.slot;
	group.views.reserve(a_count);
	for (uint32_t i = 0; i < a_count; ++i)
		if (a_views[i] != VK_NULL_HANDLE)
			group.views.push_back(a_views[i]);
	if (!group.views.empty()) {
		fsrSwapchainTeardownConfirmed = false;
		pendingFSRPresentViewGroups.push_back(std::move(group));
	}
}

void DXVKInterop::QuarantineViewsUntilFSRSwapchainTeardown(const CommandTransaction& a_transaction,
	const VkImageView* a_views, uint32_t a_count)
{
	if (a_transaction.owner != this || !a_transaction.ringLock.owns_lock() ||
		!a_views || a_transaction.slot >= pendingViewDeletes.size())
		return;
	FSRPresentViewGroup group{};
	group.slot = a_transaction.slot;
	group.views.reserve(a_count);
	for (uint32_t i = 0; i < a_count; ++i)
		if (a_views[i] != VK_NULL_HANDLE)
			group.views.push_back(a_views[i]);
	if (!group.views.empty()) {
		fsrSwapchainTeardownConfirmed = false;
		quarantinedFSRPresentViewGroups.push_back(std::move(group));
	}
}

void DXVKInterop::NotifyFSRFrameConsumed()
{
	std::lock_guard lock(commandRingMutex);
	for (auto& group : pendingFSRPresentViewGroups) {
		if (group.slot < pendingViewDeletes.size()) {
			auto& slot = pendingViewDeletes[group.slot];
			slot.insert(slot.end(), group.views.begin(), group.views.end());
		} else {
			quarantinedFSRPresentViewGroups.push_back(std::move(group));
		}
	}
	pendingFSRPresentViewGroups.clear();
}

void DXVKInterop::QuarantineUnconsumedFSRPresentViews()
{
	std::lock_guard lock(commandRingMutex);
	for (auto& group : pendingFSRPresentViewGroups)
		quarantinedFSRPresentViewGroups.push_back(std::move(group));
	pendingFSRPresentViewGroups.clear();
}

void DXVKInterop::ReleaseRetainedPresentResourcesAfterFSRSwapchainTeardown()
{
	std::lock_guard lock(commandRingMutex);
	fsrSwapchainTeardownConfirmed = true;
	for (auto& group : pendingFSRPresentViewGroups)
		quarantinedFSRPresentViewGroups.push_back(std::move(group));
	pendingFSRPresentViewGroups.clear();

	std::vector<FSRPresentViewGroup> unresolved;
	for (auto& group : quarantinedFSRPresentViewGroups) {
		if (group.slot < pendingViewDeletes.size()) {
			auto& slot = pendingViewDeletes[group.slot];
			slot.insert(slot.end(), group.views.begin(), group.views.end());
			continue;
		}
		bool destructionFaulted = false;
		if (!vulkanResourceDestructionTerminalFault && commandRingSubmissionsIdleProven &&
			vkDestroyImageView) {
			for (VkImageView& view : group.views) {
				if (view == VK_NULL_HANDLE)
					continue;
				const VulkanVoidAttempt destroyAttempt = DestroyImageViewSEH(vkDestroyImageView, device, view);
				if (destroyAttempt.completed) {
					view = VK_NULL_HANDLE;
				} else {
					view = VK_NULL_HANDLE;
					destructionFaulted = true;
					commandRingFaulted = true;
					vulkanResourceDestructionTerminalFault = true;
					break;
				}
			}
			if (destructionFaulted)
				std::fill(group.views.begin(), group.views.end(), VK_NULL_HANDLE);
		}
		if (destructionFaulted || std::find_if(group.views.begin(), group.views.end(),
				[](VkImageView a_view) { return a_view != VK_NULL_HANDLE; }) != group.views.end())
			unresolved.push_back(std::move(group));
	}
	quarantinedFSRPresentViewGroups = std::move(unresolved);

	ReleaseRetainedFSRResourcesIfSafe();
}
