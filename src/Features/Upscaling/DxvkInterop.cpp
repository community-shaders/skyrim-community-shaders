#include "DxvkInterop.h"

#include "Globals.h"

DxvkInterop* DxvkInterop::GetSingleton()
{
	static DxvkInterop singleton;
	return &singleton;
}

bool DxvkInterop::Initialize()
{
	if (available)
		return true;

	auto d3dDevice = globals::d3d::device;
	if (!d3dDevice) {
		logger::warn("[DxvkInterop] No D3D11 device available yet");
		return false;
	}

	// IDXGIVkInteropDevice is only implemented by DXVK. On native D3D11 this
	// fails cleanly and we simply report unavailable.
	winrt::com_ptr<IDXGIVkInteropDevice> dev;
	if (FAILED(d3dDevice->QueryInterface(__uuidof(IDXGIVkInteropDevice), dev.put_void()))) {
		logger::info("[DxvkInterop] IDXGIVkInteropDevice not present — not running under DXVK");
		return false;
	}

	interopDevice = dev;
	interopDevice->GetVulkanHandles(&instance, &physicalDevice, &device);
	interopDevice->GetSubmissionQueue(&queue, &queueFamilyIndex);

	if (!instance || !physicalDevice || !device || !queue) {
		logger::error("[DxvkInterop] DXVK returned null Vulkan handles");
		interopDevice = nullptr;
		return false;
	}

	// vulkan-1.dll is already loaded in-process by DXVK; grab the loader entry.
	if (HMODULE vk = GetModuleHandleW(L"vulkan-1.dll")) {
		vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			reinterpret_cast<void*>(GetProcAddress(vk, "vkGetInstanceProcAddr")));
	}
	if (!vkGetInstanceProcAddr) {
		logger::error("[DxvkInterop] Could not resolve vkGetInstanceProcAddr from vulkan-1.dll");
		interopDevice = nullptr;
		return false;
	}

	vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
		vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
	if (!vkGetDeviceProcAddr) {
		logger::error("[DxvkInterop] Could not resolve vkGetDeviceProcAddr");
		interopDevice = nullptr;
		return false;
	}

	// Resolved once for the deferred-view-delete path (BeginFrameCommandBuffer / teardown).
	vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
		vkGetDeviceProcAddr(device, "vkDestroyImageView"));

	// Log the physical device for confirmation (single shared device).
	if (auto pfnProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
			vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"))) {
		VkPhysicalDeviceProperties props{};
		pfnProps(physicalDevice, &props);
		logger::info("[DxvkInterop] Bridged to DXVK Vulkan device: '{}' (API {}.{}.{}), queueFamily {}",
			props.deviceName,
			VK_API_VERSION_MAJOR(props.apiVersion),
			VK_API_VERSION_MINOR(props.apiVersion),
			VK_API_VERSION_PATCH(props.apiVersion),
			queueFamilyIndex);
	} else {
		logger::info("[DxvkInterop] Bridged to DXVK Vulkan device (queueFamily {})", queueFamilyIndex);
	}

	available = true;
	return true;
}

bool DxvkInterop::GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
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

void DxvkInterop::FlushRenderingCommands() const
{
	if (interopDevice)
		interopDevice->FlushRenderingCommands();
}

void DxvkInterop::WaitDeviceIdle() const
{
	if (!interopDevice)
		return;
	// DXVK-safe drain. Do NOT call vkDeviceWaitIdle directly here: DXVK owns the queues from its own
	// submission thread, and vkDeviceWaitIdle requires external synchronization of ALL queue access —
	// an unsynchronized external call races DXVK's submit thread (and, while DLSS-G is active, waits on
	// Streamline's async pacer queue) and HANGS. That was the frame-gen toggle freeze. Instead flush
	// pending D3D11 work to the GPU and take/release DXVK's submission lock, which drains DXVK's pending
	// submissions the DXVK-managed way without the unsafe global wait.
	FlushRenderingCommands();
	LockSubmissionQueue();
	ReleaseSubmissionQueue();
}

void DxvkInterop::LockSubmissionQueue() const
{
	if (interopDevice)
		interopDevice->LockSubmissionQueue();
}

void DxvkInterop::ReleaseSubmissionQueue() const
{
	if (interopDevice)
		interopDevice->ReleaseSubmissionQueue();
}

bool DxvkInterop::CreateCommandResources(uint32_t a_framesInFlight)
{
	std::lock_guard lock(commandRingMutex);
	if (!available)
		return false;
	if (commandPool != VK_NULL_HANDLE)
		return true;

	framesInFlight = a_framesInFlight ? a_framesInFlight : 1;

	VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndex;
	if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
		logger::error("[DxvkInterop] vkCreateCommandPool failed");
		commandPool = VK_NULL_HANDLE;
		return false;
	}

	commandBuffers.resize(framesInFlight, VK_NULL_HANDLE);
	VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = framesInFlight;
	if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
		logger::error("[DxvkInterop] vkAllocateCommandBuffers failed");
		DestroyCommandResources();
		return false;
	}

	// Fences start signaled so the first BeginFrameCommandBuffer doesn't block.
	commandFences.resize(framesInFlight, VK_NULL_HANDLE);
	VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for (uint32_t i = 0; i < framesInFlight; ++i) {
		if (vkCreateFence(device, &fenceInfo, nullptr, &commandFences[i]) != VK_SUCCESS) {
			logger::error("[DxvkInterop] vkCreateFence failed");
			DestroyCommandResources();
			return false;
		}
	}

	commandFrameIndex = 0;
	pendingViewDeletes.assign(framesInFlight, {});
	logger::info("[DxvkInterop] Command ring created ({} frames in flight, queueFamily {})", framesInFlight, queueFamilyIndex);
	return true;
}

void DxvkInterop::DestroyCommandResources()
{
	std::lock_guard lock(commandRingMutex);
	if (device == VK_NULL_HANDLE)
		return;

	// Ensure no in-flight submissions before tearing down.
	for (auto f : commandFences) {
		if (f != VK_NULL_HANDLE)
			vkWaitForFences(device, 1, &f, VK_TRUE, UINT64_MAX);
	}
	// All submissions have drained — free any views still queued for deferred destruction.
	if (vkDestroyImageView) {
		for (auto& slot : pendingViewDeletes)
			for (VkImageView v : slot)
				if (v != VK_NULL_HANDLE)
					vkDestroyImageView(device, v, nullptr);
	}
	pendingViewDeletes.clear();
	for (auto f : commandFences) {
		if (f != VK_NULL_HANDLE)
			vkDestroyFence(device, f, nullptr);
	}
	commandFences.clear();

	if (commandPool != VK_NULL_HANDLE) {
		// Freeing the pool frees its command buffers.
		vkDestroyCommandPool(device, commandPool, nullptr);
		commandPool = VK_NULL_HANDLE;
	}
	commandBuffers.clear();
	framesInFlight = 0;
	commandFrameIndex = 0;
}

void DxvkInterop::DrainCommandRing()
{
	std::lock_guard lock(commandRingMutex);
	if (commandPool == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
		return;

	// Wait every in-flight submission so no foreign upscaler dispatch still references interop images
	// about to be destroyed. Fences are left signalled (not reset) — the next BeginFrameCommandBuffer
	// waits + resets them as usual, so this is a no-op stall on the fast path afterwards.
	if (!commandFences.empty())
		vkWaitForFences(device, static_cast<uint32_t>(commandFences.size()), commandFences.data(), VK_TRUE, UINT64_MAX);

	// The GPU is now idle w.r.t. our submissions — free views still queued for deferred destruction so
	// they are not double-processed when their slot is later reused.
	if (vkDestroyImageView) {
		for (auto& slot : pendingViewDeletes) {
			for (VkImageView v : slot)
				if (v != VK_NULL_HANDLE)
					vkDestroyImageView(device, v, nullptr);
			slot.clear();
		}
	}
}

VkCommandBuffer DxvkInterop::BeginFrameCommandBuffer()
{
	if (commandPool == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	// Vulkan command pools and their command buffers require external synchronization. This ring is
	// used by both the render thread and Streamline's present/tag path, so retain the lock until the
	// matching SubmitFrameCommandBuffer call has ended and submitted this recording.
	commandRingMutex.lock();
	if (commandPool == VK_NULL_HANDLE) {
		commandRingMutex.unlock();
		return VK_NULL_HANDLE;
	}

	// NEVER-BLOCKING acquire (task #88 experiment): a fence wait here deadlocks under SL's
	// eBlockPresentingClientQueue (the queue is held at each present until FG completes, so ring
	// fences signal late — and at FG engagement transitions possibly never while we're the ones
	// starving it). Instead: take any already-signaled slot; if none, GROW the ring; only at a
	// hard cap fall back to waiting (logged — if that fires under the SL-block mode, the
	// experiment failed).
	constexpr uint32_t kMaxRingDepth = 64;
	uint32_t next = (commandFrameIndex + 1) % framesInFlight;
	if (vkGetFenceStatus(device, commandFences[next]) != VK_SUCCESS) {
		uint32_t freeSlot = UINT32_MAX;
		for (uint32_t i = 0; i < framesInFlight; ++i) {
			const uint32_t cand = (next + i) % framesInFlight;
			if (vkGetFenceStatus(device, commandFences[cand]) == VK_SUCCESS) {
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
			VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			if (vkAllocateCommandBuffers(device, &allocInfo, &newCb) == VK_SUCCESS &&
				vkCreateFence(device, &fenceInfo, nullptr, &newFence) == VK_SUCCESS) {
				commandBuffers.push_back(newCb);
				commandFences.push_back(newFence);
				pendingViewDeletes.emplace_back();
				next = framesInFlight;
				++framesInFlight;
				logger::info("[DxvkInterop] Command ring grown to {} (all slots in flight)", framesInFlight);
			} else {
				logger::warn("[DxvkInterop] ring growth failed — falling back to fence wait");
				vkWaitForFences(device, 1, &commandFences[next], VK_TRUE, UINT64_MAX);
			}
		} else {
			static bool s_warned = false;
			if (!s_warned) {
				s_warned = true;
				logger::warn("[DxvkInterop] ring at max depth {} with all slots in flight — waiting (deadlock risk under SL queue-block)", kMaxRingDepth);
			}
			vkWaitForFences(device, 1, &commandFences[next], VK_TRUE, UINT64_MAX);
		}
	}
	commandFrameIndex = next;
	VkFence fence = commandFences[commandFrameIndex];
	VkCommandBuffer cb = commandBuffers[commandFrameIndex];

	// That submission is now complete, so any transient views tagged to this slot are no longer
	// referenced by the GPU — destroy them here rather than blocking the submit path on a fence.
	if (commandFrameIndex < pendingViewDeletes.size()) {
		auto& dead = pendingViewDeletes[commandFrameIndex];
		if (vkDestroyImageView) {
			for (VkImageView v : dead)
				if (v != VK_NULL_HANDLE)
					vkDestroyImageView(device, v, nullptr);
		}
		dead.clear();
	}

	vkResetFences(device, 1, &fence);
	vkResetCommandBuffer(cb, 0);

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
		logger::error("[DxvkInterop] vkBeginCommandBuffer failed");
		commandRingMutex.unlock();
		return VK_NULL_HANDLE;
	}
	return cb;
}

bool DxvkInterop::SubmitFrameCommandBuffer(VkCommandBuffer a_commandBuffer, bool a_waitIdle)
{
	// BeginFrameCommandBuffer deliberately leaves this held across Streamline's recording calls.
	std::unique_lock<std::recursive_mutex> transactionLock(commandRingMutex, std::adopt_lock);
	if (commandPool == VK_NULL_HANDLE || a_commandBuffer == VK_NULL_HANDLE)
		return false;

	if (vkEndCommandBuffer(a_commandBuffer) != VK_SUCCESS) {
		logger::error("[DxvkInterop] vkEndCommandBuffer failed");
		return false;
	}

	VkFence fence = commandFences[commandFrameIndex];

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &a_commandBuffer;

	// Flush DXVK's pending D3D11 work so it is visible to the queue before our
	// submission, then submit under the lock so DXVK's own worker thread doesn't
	// race us on the shared graphics queue.
	FlushRenderingCommands();
	LockSubmissionQueue();
	VkResult vr = vkQueueSubmit(queue, 1, &submitInfo, fence);
	ReleaseSubmissionQueue();

	if (vr != VK_SUCCESS) {
		logger::error("[DxvkInterop] vkQueueSubmit failed ({})", (int)vr);
		return false;
	}

	if (a_waitIdle) {
		vr = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		if (vr != VK_SUCCESS) {
			logger::error("[DxvkInterop] upscaler completion wait failed ({})", static_cast<int>(vr));
			return false;
		}
	}

	return true;
}

void DxvkInterop::QueueViewsForDeferredDelete(const VkImageView* a_views, uint32_t a_count)
{
	std::lock_guard lock(commandRingMutex);
	// Tag the views to the ring slot the just-submitted command buffer used (commandFrameIndex is
	// only advanced by the next BeginFrameCommandBuffer). They are destroyed when that slot's fence
	// signals at reuse — see BeginFrameCommandBuffer.
	if (!a_views || commandFrameIndex >= pendingViewDeletes.size())
		return;
	auto& slot = pendingViewDeletes[commandFrameIndex];
	for (uint32_t i = 0; i < a_count; ++i)
		if (a_views[i] != VK_NULL_HANDLE)
			slot.push_back(a_views[i]);
}
