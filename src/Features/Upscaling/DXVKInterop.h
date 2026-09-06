#pragma once

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

/** @brief Accesses DXVK's Vulkan device through its D3D11 interop interfaces. */
class DXVKInterop
{
public:
	enum class PresenterEncoding : uint8_t
	{
		kUnknown,
		kSDR,
		kHDR10,
		kHDR10ScRGBFallback,
	};

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
		friend class DXVKInterop;

		CommandTransaction(DXVKInterop* a_owner, uint32_t a_slot, VkCommandBuffer a_commandBuffer,
			std::unique_lock<std::recursive_mutex>&& a_ringLock) :
			owner(a_owner), slot(a_slot), commandBuffer(a_commandBuffer), ringLock(std::move(a_ringLock))
		{}

		DXVKInterop* owner = nullptr;
		uint32_t slot = UINT32_MAX;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		bool submitted = false;
		bool submissionMayBeInFlight = false;
		std::unique_lock<std::recursive_mutex> ringLock;
	};

	static DXVKInterop* GetSingleton();

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
	/** @brief True once a present has completed without consuming its registered one-shot semaphore. */
	[[nodiscard]] bool IsPresentWaitUnattachedForSwapchain() const;
	/** @brief Forgets the unattached verdict; call when the swapchain is recreated. */
	void ResetPresentWaitUnattachedForSwapchain();
	/** @brief True once VK_ERROR_DEVICE_LOST has been observed. Terminal for the session. */
	bool IsDeviceLost() const;
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

	/** @brief Whether the latest present-tag submission still has a registered semaphore. */
	bool PushPendingPresentWaitSemaphore();
	/** @brief Whether a registered present-wait semaphore is still tracked. */
	bool HasPendingPresentWaitSemaphore() const;
	/** @brief Discards a registered semaphore after proving its signal submission complete. */
	[[nodiscard]] bool DiscardPendingPresentWaitSemaphore();
	/** @brief Reconciles the one-shot semaphore after DXVK acknowledges the outer present. */
	void NotifyPresentWaitQueued();

	/** @brief Defers image-view destruction until the current ring slot completes. */
	void QueueViewsForDeferredDelete(const CommandTransaction& a_transaction,
		const VkImageView* a_views, uint32_t a_count);
	/** @brief Holds D3D resources until the current ring slot completes. */
	void QueueResourcesForDeferredRelease(const CommandTransaction& a_transaction,
		ID3D11Resource* const* a_resources, uint32_t a_count);
	/** @brief Quarantines accepted views when a failed present removed the plugin frame without proving consumption. */

private:
	DXVKInterop() = default;

	struct PresenterSurfaceState
	{
		uint64_t serial = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkColorSpaceKHR requestedColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkColorSpaceKHR effectiveColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	};


	struct PresentWaitSubmission
	{
		uint32_t slot = UINT32_MAX;
		uint64_t generation = 0;
	};

	using GetPresenterSurfaceStateFn = uint64_t (*)(uint32_t*, uint32_t*, uint32_t*);

	/// dxvkEnqueueInteropCommandBuffer @120: hands a recorded command buffer to DXVK's own
	/// submission thread and registers the signal semaphore in the present-wait FIFO in one
	/// call. Returns the generation, or 0 on rejection.
	using EnqueueInteropCommandBufferFn = uint64_t (*)(VkCommandBuffer, VkSemaphore, VkFence);

	static VkColorSpaceKHR RequestedPresenterColorSpace(bool a_hdr);
	static PresenterEncoding ClassifyPresenterEncoding(const PresenterSurfaceState& a_state);
	static bool PresenterStateMatches(
		const PresenterSurfaceState& a_state, VkColorSpaceKHR a_requestedColorSpace);
	bool ClearReleasedPresentWaitsAfterIdle();
	void LatchPresentWaitTerminalFault(const char* a_operation, DWORD a_exceptionCode = 0);

	bool available = false;

	winrt::com_ptr<IDXGIVkInteropDevice> interopDevice;

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queueFamilyIndex = 0;

	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
	PFN_vkDestroyImageView vkDestroyImageView = nullptr;
	GetPresenterSurfaceStateFn getPresenterSurfaceState = nullptr;

	mutable std::mutex presenterStateMutex;
	PresenterSurfaceState observedPresenterState;
	PresenterSurfaceState committedPresenterState;
	bool presenterTransitionPending = false;
	uint64_t presenterTransitionBaselineSerial = 0;
	uint32_t presenterTransitionFrameCount = 0;
	VkColorSpaceKHR presenterTransitionRequestedColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	mutable std::recursive_mutex commandRingMutex;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers;
	std::vector<VkFence> commandFences;
	std::vector<VkSemaphore> presentWaitSemaphores;
	std::vector<bool> presentWaitInUse;
	uint32_t pushedPresentWaitSlot = UINT32_MAX;
	uint64_t pushedPresentWaitGeneration = 0;
	std::vector<PresentWaitSubmission> outstandingPresentWaitSubmissions;
	EnqueueInteropCommandBufferFn enqueueInteropCommandBuffer = nullptr;
	uint32_t (*getPresentWaitSemaphoreState)(uint64_t) = nullptr;
	uint32_t (*clearPresentWaitSemaphore)(uint64_t) = nullptr;
	uint32_t (*cancelPresentWaitSemaphore)(VkSemaphore) = nullptr;
	uint32_t (*releaseQueuedPresentWaitSemaphoresAfterIdle)() = nullptr;
	bool presentWaitInteropTerminalFault = false;
	// Set when a present completed without consuming the one-shot semaphore registered for
	// it: the presenter has shown it will not attach present-wait semaphores to this
	// swapchain (its frame-generation ownership is latched at creation, and can disagree
	// with the live query). Requesting one every frame after that only manufactures an
	// orphaned semaphore per present. Cleared when the swapchain is recreated.
	bool presentWaitUnattachedForSwapchain = false;
	// Latched on VK_ERROR_DEVICE_LOST. Terminal: a lost device is never recovered, so every
	// completion-proof path must fail fast instead of retrying it once per frame.
	bool deviceLost = false;
	bool synchronousPresentControlAvailable = false;
	bool presentQueueSplit = false;
	bool commandRingFaulted = false;
	bool vulkanResourceDestructionTerminalFault = false;
	mutable bool commandRingSubmissionsIdleProven = false;
	// Indexed with the command ring.
	// Resources held forever once Vulkan destruction has terminally faulted: leaking them is
	// strictly better than risking a use-after-free on a device that is already unsafe.
	std::vector<std::vector<VkImageView>> pendingViewDeletes;
	std::vector<std::vector<winrt::com_ptr<ID3D11Resource>>> pendingResourceReleases;
	// FFX may consume tagged images on its own queues after host evaluation.
	uint32_t framesInFlight = 0;
	uint32_t commandFrameIndex = 0;
};
