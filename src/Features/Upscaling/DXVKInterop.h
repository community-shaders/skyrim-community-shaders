#pragma once

#include "DXVKPresenterState.h"

#include <cs_dxvk_api.h>

// DXVK COM interfaces used to access its Vulkan device, queue, and backing images.
// Keep the declarations ABI-compatible with DXVK's dxgi_interfaces.h.

#include <d3d11.h>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>
#include <winrt/base.h>

struct IDXGIVkInteropDevice;

enum class DXVKPresentWaitState : uint32_t
{
	kNone = 0,
	kPending = 1,
	kQueued = 2,
	kUncertain = 3,
	kReleased = 4,
};

// DXVK's public Reflex interop ABI. Keep this declaration in sync with
// ID3DLowLatencyDevice in dxvk/src/d3d11/d3d11_interfaces.h.
MIDL_INTERFACE("f3112584-41f9-348d-a59b-00b7e1d285d6")
ID3DLowLatencyDevice : public IUnknown
{
	virtual BOOL STDMETHODCALLTYPE SupportsLowLatency() = 0;
	virtual HRESULT STDMETHODCALLTYPE LatencySleep() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetLatencySleepMode(
		BOOL LowLatencyEnable, BOOL LowLatencyBoost, UINT32 MinIntervalUs) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetLatencyMarker(UINT64 FrameId, UINT32 MarkerType) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetLatencyInfo(void* pLowLatencyResults) = 0;
};

MIDL_INTERFACE("5546cf8c-77e7-4341-b05d-8d4d5000e77d")
IDXGIVkInteropSurface : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDXGIVkInteropDevice * *ppDevice) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
		VkImage * pHandle,
		VkImageLayout * pLayout,
		VkImageCreateInfo * pInfo) = 0;
};

MIDL_INTERFACE("e2ef5fa5-dc21-4af7-90c4-f67ef6a09323")
IDXGIVkInteropDevice : public IUnknown
{
	virtual void STDMETHODCALLTYPE GetVulkanHandles(
		VkInstance * pInstance,
		VkPhysicalDevice * pPhysDev,
		VkDevice * pDevice) = 0;
	virtual void STDMETHODCALLTYPE GetSubmissionQueue(
		VkQueue * pQueue,
		uint32_t* pQueueFamilyIndex) = 0;
	virtual void STDMETHODCALLTYPE TransitionSurfaceLayout(
		IDXGIVkInteropSurface * pSurface,
		const VkImageSubresourceRange* pSubresources,
		VkImageLayout OldLayout,
		VkImageLayout NewLayout) = 0;
	virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
	virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
	virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
};

/**
 * Process-lifetime Vulkan device and submission context exposed by DXVK.
 *
 * Device handles become immutable after Initialize. Command slots, present-wait
 * generations, and deferred retirement form one submission lifecycle protected
 * by commandRingMutex. The recursive mutex permits conservative recovery to
 * drain nested retirement work without losing slot identity.
 */
class VulkanDeviceContext
{
	friend class UpscalingRuntime;
public:
	using PresenterEncoding = DXVKPresenterState::Encoding;

	class CommandTransaction
	{
	public:
		CommandTransaction() = default;
		CommandTransaction(CommandTransaction&&) noexcept = default;
		CommandTransaction& operator=(CommandTransaction&&) noexcept = default;
		CommandTransaction(const CommandTransaction&) = delete;
		CommandTransaction& operator=(const CommandTransaction&) = delete;

		explicit operator bool() const
		{
			return owner != nullptr && commandBuffer != VK_NULL_HANDLE && ringLock.owns_lock();
		}
		VkCommandBuffer GetCommandBuffer() const { return commandBuffer; }
		/** @brief Whether a fault left this transaction's queue submission potentially in flight. */
		bool SubmissionMayBeInFlight() const { return submissionMayBeInFlight; }

	private:
		friend class VulkanDeviceContext;

		CommandTransaction(VulkanDeviceContext* a_owner, uint32_t a_slot, VkCommandBuffer a_commandBuffer,
			std::unique_lock<std::recursive_mutex>&& a_ringLock) :
			owner(a_owner), slot(a_slot), commandBuffer(a_commandBuffer), ringLock(std::move(a_ringLock))
		{}

		VulkanDeviceContext* owner = nullptr;
		uint32_t slot = UINT32_MAX;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		bool submitted = false;
		bool submissionMayBeInFlight = false;
		std::unique_lock<std::recursive_mutex> ringLock;
	};

	/** @brief Resolves DXVK's interop interfaces and Vulkan handles. */
	bool Initialize();

	/** @brief Whether the DXVK Vulkan device was resolved successfully. */
	bool IsAvailable() const { return available; }

	/** @brief Reads the latest successfully created presenter surface state from DXVK. */
	bool RefreshPresenterSurfaceState();
	/** @brief Latches a presenter state for the render frame at a render boundary. */
	void CommitPresenterSurfaceStateForRenderFrame();
	/** @brief Starts a color-space transition before changing or recreating the swap chain.
	 *  @param a_hdr The output mode the recreated presenter must report.
	 *  @param a_requireNewSerial Whether an explicit recreation must advance the presenter serial.
	 */
	void BeginPresenterColorSpaceTransition(bool a_hdr, bool a_requireNewSerial = false);
	/** @brief Cancels a failed color-space transition for the requested output mode. */
	void CancelPresenterColorSpaceTransition(bool a_hdr);
	/** @brief Returns the effective presenter encoding latched for this render frame. */
	PresenterEncoding GetPresenterEncodingForFrame() const;
	/** @brief Returns the Vulkan surface format latched for this render frame. */
	VkFormat GetPresenterFormatForFrame() const;
	/** @brief Whether the latched presenter state exactly matches this frame's output mode. */
	bool IsPresenterStateReadyForFrame(bool a_hdr) const;

	VkInstance GetInstance() const { return instance; }
	VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
	VkDevice GetDevice() const { return device; }
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr() const { return vkGetInstanceProcAddr; }
	PFN_vkGetDeviceProcAddr GetDeviceProcAddr() const { return vkGetDeviceProcAddr; }

	/** @brief Whether DXVK can drive Reflex for the Vulkan swapchain it presents. */
	bool ReflexAvailable() const;
	/** @brief Configures DXVK's VK_NV_low_latency2 controller and render interval. */
	bool SetReflexMode(bool a_enable, bool a_boost, uint32_t a_minIntervalUs);
	/** @brief Performs the frame-begin sleep through DXVK's latency controller. */
	bool ReflexSleep();
	/** @brief Adds an application marker to DXVK's frame-to-present mapping. */
	bool SetReflexMarker(uint64_t a_frameId, uint32_t a_marker);

	/** @brief Maps a D3D11 resource to its backing DXVK image. */
	bool GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
		VkImageLayout* a_outLayout = nullptr, VkImageCreateInfo* a_outInfo = nullptr) const;

	/** @brief Drains DXVK submissions without racing its queue thread. */
	[[nodiscard]] bool WaitDeviceIdle();

	/** @brief Creates the Streamline command-buffer ring. */
	bool CreateCommandResources(uint32_t a_framesInFlight = 3);

	/** @brief Destroys the command pool, command buffers and fences. */
	void DestroyCommandResources();

	/** @brief Drains ring submissions before interop resources are destroyed. */
	[[nodiscard]] bool DrainCommandRing();

	/** @brief Whether the command ring is ready (CreateCommandResources succeeded). */
	bool CommandResourcesReady() const;
	/** @brief Whether an ambiguous submission fault quarantined the command ring. */
	bool HasCommandRingFault() const;
	/** @brief Recreates a quarantined command ring after proving the Vulkan device idle. */
	[[nodiscard]] bool RecoverCommandRing();
	/** @brief Whether a tag submission can be GPU-ordered before DXVK's next present. */
	bool PresentWaitInteropReady() const;
	/** @brief Whether frame generation shares DXVK's game submission queue. */
	bool FrameGenerationQueueInteropReady() const;

	/** @brief Begins an available command buffer from the ring. */
	CommandTransaction BeginFrameCommandBuffer();

	/** @brief Submits a ring command buffer on DXVK's queue. */
	bool SubmitFrameCommandBuffer(CommandTransaction& a_transaction,
		bool a_signalForNextPresent = false);

	/** @brief Commits the DXVK-reserved wait generation to present-lifetime tracking. */
	bool CommitPendingPresentWait();
	/** @brief Whether an accepted tag submission still needs present-lifetime tracking. */
	bool HasPendingPresentWaitSemaphore() const;
	/** @brief Abandons a reserved wait safely after proving the device idle. */
	[[nodiscard]] bool DiscardPendingPresentWaitSemaphore();
	/** @brief Reconciles the one-shot semaphore after DXVK acknowledges the outer present. */
	void NotifyPresentWaitQueued();
	/** @brief Associates Streamline input completion with the command-ring slot used by a present. */
	[[nodiscard]] bool TrackInputCompletion(uint64_t a_presentWaitGeneration,
		VkSemaphore a_semaphore, uint64_t a_value);

	/** @brief Defers image-view destruction until the current ring slot completes. */
	void QueueViewsForDeferredDelete(const CommandTransaction& a_transaction,
		const VkImageView* a_views, uint32_t a_count);
	/** @brief Holds D3D resources until the current ring slot completes. */
	void QueueResourcesForDeferredRelease(const CommandTransaction& a_transaction,
		ID3D11Resource* const* a_resources, uint32_t a_count);
	/** @brief Holds resources consumed by frame generation across the matching outer present. */
	void QueueResourcesForPresent(const CommandTransaction& a_transaction,
		ID3D11Resource* const* a_resources, uint32_t a_count);
	/** @brief Retains backing images permanently after ambiguous Vulkan view destruction. */
	void QuarantineResourcesAfterVulkanDestructionFault(
		ID3D11Resource* const* a_resources, uint32_t a_count);
	/** @brief Holds accepted FSR views until the matching outer present consumes them. */
	void QueueViewsForFSRPresent(const CommandTransaction& a_transaction,
		const VkImageView* a_views, uint32_t a_count);
	/** @brief Holds partially accepted FSR views until confirmed FFX teardown. */
	void QuarantineViewsUntilFSRSwapchainTeardown(const CommandTransaction& a_transaction,
		const VkImageView* a_views, uint32_t a_count);
	/** @brief Fence-gates accepted FSR views after the plugin consumes or discards them. */
	void NotifyFSRFrameConsumed();
	/** @brief Quarantines accepted views when a failed present removed the plugin frame without proving consumption. */
	void QuarantineUnconsumedFSRPresentViews();
	/** @brief Releases FSR present lifetimes after its swapchain teardown completed successfully. */
	void ReleaseRetainedPresentResourcesAfterFSRSwapchainTeardown();

private:
	VulkanDeviceContext() = default;

	bool ClearReleasedPresentWaitsAfterIdle();
	bool IsInputCompletionReady(uint32_t a_slot);
	void ReleaseRetainedFSRResourcesIfSafe();


	// Submission, present-wait, and retirement records deliberately share one
	// synchronization owner because all three are indexed by the same slot.
	struct PresentWaitSubmission { uint32_t slot = UINT32_MAX; uint64_t generation = 0; };
	struct InputCompletion { VkSemaphore semaphore = VK_NULL_HANDLE; uint64_t value = 0; };
	struct FSRPresentViewGroup { uint32_t slot = UINT32_MAX; std::vector<VkImageView> views; };
	mutable std::recursive_mutex commandRingMutex;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers;
	std::vector<VkFence> commandFences;
	bool commandRingFaulted = false;
	mutable bool commandRingSubmissionsIdleProven = false;
	uint32_t framesInFlight = 0;
	uint32_t commandFrameIndex = 0;
	std::vector<VkSemaphore> presentWaitSemaphores;
	std::vector<bool> presentWaitInUse;
	std::vector<InputCompletion> inputCompletions;
	uint32_t pendingPresentWaitSlot = UINT32_MAX;
	uint64_t pendingPresentWaitGeneration = 0;
	std::vector<PresentWaitSubmission> outstandingPresentWaitSubmissions;
	PFN_csDxvkEnqueueInteropCommandBuffer enqueueInteropCommandBuffer = nullptr;
	PFN_csDxvkGetPresentWaitSemaphoreState getPresentWaitSemaphoreState = nullptr;
	PFN_csDxvkClearPresentWaitSemaphore clearPresentWaitSemaphore = nullptr;
	PFN_csDxvkCancelPresentWaitSemaphore cancelPresentWaitSemaphore = nullptr;
	PFN_csDxvkReleaseQueuedPresentWaitSemaphoresAfterIdle releaseQueuedPresentWaitSemaphoresAfterIdle = nullptr;
	bool presentWaitInteropTerminalFault = false;
	bool synchronousPresentControlAvailable = false;
	bool presentQueueSplit = false;
	bool available = false;
	winrt::com_ptr<IDXGIVkInteropDevice> interopDevice;
	winrt::com_ptr<ID3DLowLatencyDevice> lowLatencyDevice;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queueFamilyIndex = 0;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
	PFN_vkDestroyImageView vkDestroyImageView = nullptr;
	bool submissionQueueLockUncertain = false;
	bool vulkanResourceDestructionTerminalFault = false;
	std::vector<std::vector<VkImageView>> pendingViewDeletes;
	std::vector<std::vector<winrt::com_ptr<ID3D11Resource>>> pendingResourceReleases;
	std::vector<winrt::com_ptr<ID3D11Resource>> retainedPresentResources;
	std::vector<FSRPresentViewGroup> pendingFSRPresentViewGroups;
	std::vector<FSRPresentViewGroup> quarantinedFSRPresentViewGroups;
	bool fsrSwapchainTeardownConfirmed = false;
	DXVKPresenterState presenterState;

};
