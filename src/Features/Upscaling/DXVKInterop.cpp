#include "DXVKInterop.h"

#include "../../DxvkLoader.h"
#include "Globals.h"

#include <algorithm>

namespace
{
	struct EnqueueInteropAttempt
	{
		VkResult endResult = VK_ERROR_DEVICE_LOST;
		VkResult resetResult = VK_ERROR_DEVICE_LOST;
		uint64_t generation = 0;
		DWORD exceptionCode = 0;
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

	EnqueueInteropAttempt EnqueueInteropSEH(IDXGIVkInteropDevice* a_interopDevice,
		VkDevice a_device, VkCommandBuffer a_commandBuffer, VkSemaphore a_signalSemaphore,
		VkFence a_fence, uint64_t (*a_enqueue)(VkCommandBuffer, VkSemaphore, VkFence)) noexcept
	{
		EnqueueInteropAttempt attempt{};
		__try {
			attempt.endResult = vkEndCommandBuffer(a_commandBuffer);
			if (attempt.endResult == VK_SUCCESS) {
				attempt.resetResult = vkResetFences(a_device, 1, &a_fence);
				if (attempt.resetResult == VK_SUCCESS) {
					a_interopDevice->FlushRenderingCommands();
					attempt.generation = a_enqueue(a_commandBuffer, a_signalSemaphore, a_fence);
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
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

bool VulkanDeviceContext::RefreshPresenterSurfaceState()
{
	return presenterState.Refresh();
}

void VulkanDeviceContext::CommitPresenterSurfaceStateForRenderFrame()
{
	presenterState.CommitForRenderFrame();
}

void VulkanDeviceContext::BeginPresenterColorSpaceTransition(bool a_hdr, bool a_requireNewSerial)
{
	presenterState.BeginTransition(a_hdr, a_requireNewSerial);
}

void VulkanDeviceContext::CancelPresenterColorSpaceTransition(bool a_hdr)
{
	presenterState.CancelTransition(a_hdr);
}

VulkanDeviceContext::PresenterEncoding VulkanDeviceContext::GetPresenterEncodingForFrame() const
{
	return presenterState.GetEncodingForFrame();
}

VkFormat VulkanDeviceContext::GetPresenterFormatForFrame() const
{
	return presenterState.GetFormatForFrame();
}

bool VulkanDeviceContext::IsPresenterStateReadyForFrame(bool a_hdr) const
{
	return presenterState.IsReadyForFrame(a_hdr);
}

bool VulkanDeviceContext::Initialize()
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
	winrt::com_ptr<ID3DLowLatencyDevice> reflex;
	if (SUCCEEDED(d3dDevice->QueryInterface(__uuidof(ID3DLowLatencyDevice), reflex.put_void())) &&
		reflex->SupportsLowLatency()) {
		lowLatencyDevice = std::move(reflex);
		logger::info("[DXVKInterop] DXVK-presented swapchain Reflex controller available");
	} else {
		logger::info("[DXVKInterop] DXVK-presented swapchain Reflex controller unavailable");
	}
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

	const auto api = DxvkLoader::GetPresentWaitInterop();
	enqueueInteropCommandBuffer = api.enqueueInteropCommandBuffer;
	getPresentWaitSemaphoreState = api.getPresentWaitSemaphoreState;
	clearPresentWaitSemaphore = api.clearPresentWaitSemaphore;
	cancelPresentWaitSemaphore = api.cancelPresentWaitSemaphore;
	releaseQueuedPresentWaitSemaphoresAfterIdle = api.releaseQueuedPresentWaitSemaphoresAfterIdle;
	synchronousPresentControlAvailable = DxvkLoader::SupportsSynchronousPresent();
	presenterState.SetQuery(api.getPresenterSurfaceState);
	char splitValue[2]{};
	presentQueueSplit = GetEnvironmentVariableA("DXVK_PRESENT_QUEUE_SPLIT", splitValue,
		static_cast<DWORD>(std::size(splitValue))) != 0 && splitValue[0] == '1';
	if (!enqueueInteropCommandBuffer || !getPresentWaitSemaphoreState || !clearPresentWaitSemaphore ||
		!cancelPresentWaitSemaphore || !releaseQueuedPresentWaitSemaphoresAfterIdle)
		logger::warn("[DXVKInterop] acknowledged present-wait semaphore interop is unavailable - DLSS-G disabled");
	if (!synchronousPresentControlAvailable)
		logger::warn("[DXVKInterop] dxvkSetSyncPresent is unavailable - DLSS-G disabled");
	if (!api.getPresenterSurfaceState)
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

bool VulkanDeviceContext::ReflexAvailable() const
{
	return lowLatencyDevice != nullptr;
}

bool VulkanDeviceContext::SetReflexMode(bool a_enable, bool a_boost, uint32_t a_minIntervalUs)
{
	return lowLatencyDevice && SUCCEEDED(lowLatencyDevice->SetLatencySleepMode(
		a_enable, a_boost, a_minIntervalUs));
}

bool VulkanDeviceContext::ReflexSleep()
{
	return lowLatencyDevice && SUCCEEDED(lowLatencyDevice->LatencySleep());
}

bool VulkanDeviceContext::SetReflexMarker(uint64_t a_frameId, uint32_t a_marker)
{
	return lowLatencyDevice && SUCCEEDED(lowLatencyDevice->SetLatencyMarker(a_frameId, a_marker));
}

bool VulkanDeviceContext::GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
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

bool VulkanDeviceContext::WaitDeviceIdle()
{
	std::lock_guard lock(commandRingMutex);
	if (!interopDevice || !vkGetDeviceProcAddr || device == VK_NULL_HANDLE)
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
		logger::error("[DXVKInterop] vkDeviceWaitIdle failed ({})", static_cast<int>(attempt.result));
		return false;
	}
	commandRingSubmissionsIdleProven = true;
	if (attempt.releasedPresentWaitCount)
		logger::debug("[DXVKInterop] released {} queued present waits after device idle",
			attempt.releasedPresentWaitCount);
	return true;
}

bool VulkanDeviceContext::ClearReleasedPresentWaitsAfterIdle()
{
	if (presentWaitInteropTerminalFault || !getPresentWaitSemaphoreState || !clearPresentWaitSemaphore)
		return false;

	const auto latchTerminalFault = [&](const char* a_operation, DWORD a_exceptionCode = 0) {
		commandRingFaulted = true;
		presentWaitInteropTerminalFault = true;
		enqueueInteropCommandBuffer = nullptr;
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
		const auto state = static_cast<DXVKPresentWaitState>(stateAttempt.state);
		if (state != DXVKPresentWaitState::kReleased) {
			if (state == DXVKPresentWaitState::kUncertain || state == DXVKPresentWaitState::kNone)
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

	return true;
}

bool VulkanDeviceContext::CreateCommandResources(uint32_t a_framesInFlight)
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
	inputCompletions.assign(framesInFlight, {});

	commandFrameIndex = 0;
	pendingViewDeletes.assign(framesInFlight, {});
	pendingResourceReleases.assign(framesInFlight, {});
	logger::info("[DXVKInterop] Command ring created ({} frames in flight, queueFamily {})", framesInFlight, queueFamilyIndex);
	return true;
}

void VulkanDeviceContext::DestroyCommandResources()
{
	std::lock_guard lock(commandRingMutex);
	if (device == VK_NULL_HANDLE)
		return;
	if (vulkanResourceDestructionTerminalFault) {
		logger::error("[DXVKInterop] Vulkan resource cleanup is terminally quarantined after a destruction fault");
		return;
	}
	if (presentWaitInteropTerminalFault && pendingPresentWaitSlot != UINT32_MAX) {
		logger::error("[DXVKInterop] present-wait handle remains quarantined after a terminal bridge fault");
		return;
	}
	const bool hasRegisteredPresentWaits = !outstandingPresentWaitSubmissions.empty();
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
	inputCompletions.clear();
	pendingPresentWaitSlot = UINT32_MAX;
	pendingPresentWaitGeneration = 0;
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

bool VulkanDeviceContext::DrainCommandRing()
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
	const bool hasRegisteredPresentWaits = !outstandingPresentWaitSubmissions.empty();
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

bool VulkanDeviceContext::CommandResourcesReady() const
{
	std::lock_guard lock(commandRingMutex);
	return commandPool != VK_NULL_HANDLE && !commandRingFaulted &&
	       !vulkanResourceDestructionTerminalFault && !submissionQueueLockUncertain;
}

bool VulkanDeviceContext::HasCommandRingFault() const
{
	std::lock_guard lock(commandRingMutex);
	return commandRingFaulted || vulkanResourceDestructionTerminalFault || submissionQueueLockUncertain;
}

bool VulkanDeviceContext::RecoverCommandRing()
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

bool VulkanDeviceContext::PresentWaitInteropReady() const
{
	std::lock_guard lock(commandRingMutex);
	return enqueueInteropCommandBuffer != nullptr && getPresentWaitSemaphoreState != nullptr &&
	       clearPresentWaitSemaphore != nullptr && cancelPresentWaitSemaphore != nullptr &&
	       releaseQueuedPresentWaitSemaphoresAfterIdle != nullptr &&
	       !presentWaitInteropTerminalFault && synchronousPresentControlAvailable &&
	       !presentQueueSplit && !submissionQueueLockUncertain;
}

bool VulkanDeviceContext::FrameGenerationQueueInteropReady() const
{
	std::lock_guard lock(commandRingMutex);
	return available && !presentQueueSplit && !submissionQueueLockUncertain;
}

VulkanDeviceContext::CommandTransaction VulkanDeviceContext::BeginFrameCommandBuffer()
{
	std::unique_lock ringLock(commandRingMutex);
	if (commandPool == VK_NULL_HANDLE || commandRingFaulted || submissionQueueLockUncertain)
		return {};

	// Never block the render thread on Streamline input processing. Reuse only a
	// slot whose command submission and Streamline timeline have both completed;
	// grow the ring when all existing slots remain in flight.
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
	const bool nextInputReady = nextFenceAttempt.result == VK_SUCCESS && IsInputCompletionReady(next);
	if (commandRingFaulted)
		return {};
	if (presentWaitInUse[next] || nextFenceAttempt.result == VK_NOT_READY || !nextInputReady) {
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
			if (candidateAttempt.result == VK_SUCCESS && IsInputCompletionReady(cand)) {
				freeSlot = cand;
				break;
			}
			if (commandRingFaulted)
				return {};
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
				inputCompletions.emplace_back();
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

bool VulkanDeviceContext::SubmitFrameCommandBuffer(CommandTransaction& a_transaction,
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
		(!PresentWaitInteropReady() || pendingPresentWaitSlot != UINT32_MAX ||
		 slot >= presentWaitSemaphores.size() ||
		 presentWaitSemaphores[slot] == VK_NULL_HANDLE || presentWaitInUse[slot])) {
		logger::error("[DXVKInterop] no safe semaphore slot is available for the next present");
		return false;
	}

	VkFence& fence = commandFences[slot];
	VkSemaphore signalSemaphore = VK_NULL_HANDLE;
	if (a_signalForNextPresent)
		signalSemaphore = presentWaitSemaphores[slot];

	commandRingSubmissionsIdleProven = false;
	const EnqueueInteropAttempt attempt = EnqueueInteropSEH(interopDevice.get(), device,
		commandBuffer, signalSemaphore, fence, enqueueInteropCommandBuffer);
	if (attempt.exceptionCode || attempt.endResult != VK_SUCCESS ||
		attempt.resetResult != VK_SUCCESS || !attempt.generation) {
		commandRingFaulted = true;
		if (attempt.exceptionCode) {
			logger::error("[DXVKInterop] foreign submission enqueue faulted (SEH {:#x})",
				attempt.exceptionCode);
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
		} else
			logger::error("[DXVKInterop] DXVK rejected the foreign submission enqueue");
		return false;
	}
	a_transaction.submitted = true;

	if (a_signalForNextPresent) {
		presentWaitInUse[slot] = true;
		pendingPresentWaitSlot = slot;
		pendingPresentWaitGeneration = attempt.generation;
	}

	return true;
}

bool VulkanDeviceContext::CommitPendingPresentWait()
{
	std::lock_guard lock(commandRingMutex);
	if (!PresentWaitInteropReady() || pendingPresentWaitSlot == UINT32_MAX ||
		pendingPresentWaitSlot >= presentWaitSemaphores.size() || !pendingPresentWaitGeneration)
		return false;

	const uint32_t slot = pendingPresentWaitSlot;
	// DXVK reserved the generation when it accepted the foreign command buffer.
	// It becomes presenter-visible only after the submission thread executes the
	// signal submission, so an older present can never consume a future wait.
	outstandingPresentWaitSubmissions.push_back(
		PresentWaitSubmission{ slot, pendingPresentWaitGeneration });
	pendingPresentWaitSlot = UINT32_MAX;
	pendingPresentWaitGeneration = 0;
	return true;
}

bool VulkanDeviceContext::HasPendingPresentWaitSemaphore() const
{
	std::lock_guard lock(commandRingMutex);
	return pendingPresentWaitSlot != UINT32_MAX ||
	       (presentWaitInteropTerminalFault &&
			!outstandingPresentWaitSubmissions.empty());
}

bool VulkanDeviceContext::DiscardPendingPresentWaitSemaphore()
{
	std::lock_guard lock(commandRingMutex);
	if (presentWaitInteropTerminalFault)
		return false;
	if (pendingPresentWaitSlot == UINT32_MAX)
		return true;
	if (pendingPresentWaitSlot >= presentWaitSemaphores.size() ||
		presentWaitSemaphores[pendingPresentWaitSlot] == VK_NULL_HANDLE)
		return false;
	if (!WaitDeviceIdle())
		return false;
	if (pendingPresentWaitGeneration) {
		const PresentWaitStateAttempt clearAttempt = ClearPresentWaitSemaphoreSEH(
			clearPresentWaitSemaphore, pendingPresentWaitGeneration);
		if (clearAttempt.exceptionCode || !clearAttempt.state) {
			presentWaitInteropTerminalFault = true;
			enqueueInteropCommandBuffer = nullptr;
			logger::critical("[DXVKInterop] failed to clear abandoned reserved present wait after device idle{}",
				clearAttempt.exceptionCode ? std::format(" (SEH {:#x})", clearAttempt.exceptionCode) : "");
			return false;
		}
	}

	pendingPresentWaitSlot = UINT32_MAX;
	pendingPresentWaitGeneration = 0;
	commandRingFaulted = true;
	logger::warn("[DXVKInterop] abandoned a reserved present wait after device idle; command-ring recovery required");
	return true;
}

void VulkanDeviceContext::NotifyPresentWaitQueued()
{
	std::lock_guard lock(commandRingMutex);
	if (presentWaitInteropTerminalFault)
		return;
	const auto latchTerminalFault = [&](const char* a_operation, DWORD a_exceptionCode = 0) {
		commandRingFaulted = true;
		presentWaitInteropTerminalFault = true;
		enqueueInteropCommandBuffer = nullptr;
		if (a_exceptionCode) {
			logger::critical("[DXVKInterop] {} faulted (SEH {:#x}); present is blocked",
				a_operation, a_exceptionCode);
		} else {
			logger::critical("[DXVKInterop] {}; present is blocked", a_operation);
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
		const auto state = static_cast<DXVKPresentWaitState>(stateAttempt.state);
		if (state == DXVKPresentWaitState::kPending || state == DXVKPresentWaitState::kQueued) {
			++i;
			continue;
		}
		if (state != DXVKPresentWaitState::kReleased || submission.slot >= presentWaitInUse.size()) {
			latchTerminalFault(state == DXVKPresentWaitState::kUncertain ?
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
}

bool VulkanDeviceContext::TrackInputCompletion(uint64_t a_presentWaitGeneration,
	VkSemaphore a_semaphore, uint64_t a_value)
{
	std::lock_guard lock(commandRingMutex);
	if (!a_presentWaitGeneration || a_semaphore == VK_NULL_HANDLE || !a_value)
		return false;
	const auto submission = std::find_if(outstandingPresentWaitSubmissions.begin(),
		outstandingPresentWaitSubmissions.end(), [&](const PresentWaitSubmission& a_submission) {
			return a_submission.generation == a_presentWaitGeneration;
		});
	if (submission == outstandingPresentWaitSubmissions.end() || submission->slot >= inputCompletions.size()) {
		logger::error("[DXVKInterop] cannot associate Streamline completion value {} with present-wait generation {}",
			a_value, a_presentWaitGeneration);
		return false;
	}
	inputCompletions[submission->slot] = InputCompletion{ a_semaphore, a_value };
	return true;
}

bool VulkanDeviceContext::IsInputCompletionReady(uint32_t a_slot)
{
	// Called only while BeginFrameCommandBuffer holds the command-ring,
	// device-owner, and present-wait component locks. Keep this leaf free of
	// objects requiring unwinding because the Vulkan query is SEH-guarded.
	if (a_slot >= inputCompletions.size()) {
		commandRingFaulted = true;
		return false;
	}
	InputCompletion& completion = inputCompletions[a_slot];
	if (completion.semaphore == VK_NULL_HANDLE || !completion.value)
		return true;
	if (!vkGetDeviceProcAddr || device == VK_NULL_HANDLE) {
		commandRingFaulted = true;
		return false;
	}
	auto getCounter = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
		vkGetDeviceProcAddr(device, "vkGetSemaphoreCounterValue"));
	if (!getCounter) {
		commandRingFaulted = true;
		logger::error("[DXVKInterop] vkGetSemaphoreCounterValue is unavailable");
		return false;
	}
	uint64_t value = 0;
	VkResult result = VK_ERROR_UNKNOWN;
	__try {
		result = getCounter(device, completion.semaphore, &value);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		commandRingFaulted = true;
		logger::error("[DXVKInterop] Streamline input-completion query faulted (SEH {:#x})", GetExceptionCode());
		return false;
	}
	if (result != VK_SUCCESS) {
		commandRingFaulted = true;
		logger::error("[DXVKInterop] Streamline input-completion query failed ({})", static_cast<int>(result));
		return false;
	}
	if (value < completion.value)
		return false;
	completion = {};
	return true;
}

void VulkanDeviceContext::QueueViewsForDeferredDelete(const CommandTransaction& a_transaction,
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

void VulkanDeviceContext::QueueResourcesForDeferredRelease(const CommandTransaction& a_transaction,
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

void VulkanDeviceContext::QueueResourcesForPresent(const CommandTransaction& a_transaction,
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

void VulkanDeviceContext::QuarantineResourcesAfterVulkanDestructionFault(
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

void VulkanDeviceContext::ReleaseRetainedFSRResourcesIfSafe()
{
	if (vulkanResourceDestructionTerminalFault || !fsrSwapchainTeardownConfirmed ||
		!pendingFSRPresentViewGroups.empty() ||
		!quarantinedFSRPresentViewGroups.empty())
		return;
	const bool viewsPending = std::any_of(pendingViewDeletes.begin(), pendingViewDeletes.end(),
		[](const auto& a_slot) { return !a_slot.empty(); });
	if (!viewsPending && !retainedPresentResources.empty()) {
		logger::debug("[DXVKInterop] releasing {} resources after completed FSR teardown and view retirement",
			retainedPresentResources.size());
		retainedPresentResources.clear();
	}
}

void VulkanDeviceContext::QueueViewsForFSRPresent(const CommandTransaction& a_transaction,
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

void VulkanDeviceContext::QuarantineViewsUntilFSRSwapchainTeardown(const CommandTransaction& a_transaction,
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

void VulkanDeviceContext::NotifyFSRFrameConsumed()
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

void VulkanDeviceContext::QuarantineUnconsumedFSRPresentViews()
{
	std::lock_guard lock(commandRingMutex);
	for (auto& group : pendingFSRPresentViewGroups)
		quarantinedFSRPresentViewGroups.push_back(std::move(group));
	pendingFSRPresentViewGroups.clear();
}

void VulkanDeviceContext::ReleaseRetainedPresentResourcesAfterFSRSwapchainTeardown()
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
