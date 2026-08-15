#pragma once

// Bridge to DXVK's own Vulkan device.
//
// DXVK (the d3d11.dll/dxgi.dll proxy) renders everything on a single VkDevice.
// It exposes that device to D3D11 clients through a small set of COM interop
// interfaces (defined in DXVK's internal dxgi_interfaces.h). We re-declare the
// three we need here so CS can reach DXVK's VkInstance / VkPhysicalDevice /
// VkDevice / VkQueue and map any D3D11 texture to its backing VkImage — WITHOUT
// pulling in DXVK's internal headers and WITHOUT creating a second device.
//
// This is the foundation for Streamline interposition: the VkImages handed to
// Streamline plugins (DLSS, FSR, XeSS) are DXVK's own images, and Streamline
// submits onto DXVK's own queue via the interposer.
//
// The interfaces below are ABI-stable COM interfaces; the GUIDs match DXVK's
// dxgi_interfaces.h exactly. They are queried at runtime from the live device,
// so when running on the real (non-DXVK) D3D11 these QueryInterface calls simply
// fail and IsAvailable() returns false.

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <vector>
#include <vulkan/vulkan.h>
#include <winrt/base.h>

struct IDXGIVkInteropDevice;

// Vulkan interop surface — backing VkImage of a D3D11 texture.
MIDL_INTERFACE("5546cf8c-77e7-4341-b05d-8d4d5000e77d")
IDXGIVkInteropSurface : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDXGIVkInteropDevice * *ppDevice) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
		VkImage * pHandle,
		VkImageLayout * pLayout,
		VkImageCreateInfo * pInfo) = 0;
};

// Vulkan interop device — DXVK's VkInstance/PhysicalDevice/Device/Queue + sync.
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

MIDL_INTERFACE("e2ef5fa5-dc21-4af7-90c4-f67ef6a09324")
IDXGIVkInteropDevice1 : public IDXGIVkInteropDevice
{
	virtual void STDMETHODCALLTYPE GetSubmissionQueue1(
		VkQueue * pQueue, uint32_t* pQueueIndex, uint32_t* pQueueFamilyIndex) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateTexture2DFromVkImage(
		const D3D11_TEXTURE2D_DESC1* pDesc, VkImage image, ID3D11Texture2D** ppTexture2D) = 0;
};

// Vulkan interop factory — the VkInstance + loader entry point used by DXVK.
MIDL_INTERFACE("4c5e1b0d-b0c8-4131-bfd8-9b2476f7f408")
IDXGIVkInteropFactory : public IUnknown
{
	virtual void STDMETHODCALLTYPE GetVulkanInstance(
		VkInstance * pInstance,
		PFN_vkGetInstanceProcAddr * ppfnVkGetInstanceProcAddr) = 0;
};

/**
 * @brief Accessor for DXVK's single Vulkan device from the D3D11 proxy.
	 *
	 * Provides the Vulkan handles DXVK already uses (no second device, no DXGI
	 * shared-handle interop) plus helpers to expose CS's D3D11 textures as the
	 * VkImages that back them, and the submission-queue locking DXVK requires
	 * before foreign Vulkan work is submitted onto its queue.
	 */
class DxvkInterop
{
public:
	static DxvkInterop* GetSingleton();

	/**
		 * @brief Query the interop interfaces from the live D3D11 device.
		 * @return true when running under DXVK and all handles resolved.
		 *
		 * Safe to call once after the device exists. On native D3D11 the
		 * QueryInterface fails and this returns false (available() stays false).
		 */
	bool Initialize();

	/** @brief Whether the DXVK Vulkan device was resolved successfully. */
	bool IsAvailable() const { return available; }

	VkInstance GetInstance() const { return instance; }
	VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
	VkDevice GetDevice() const { return device; }
	uint32_t GetQueueFamilyIndex() const { return queueFamilyIndex; }
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr() const { return vkGetInstanceProcAddr; }
	PFN_vkGetDeviceProcAddr GetDeviceProcAddr() const { return vkGetDeviceProcAddr; }

	/**
		 * @brief Map a D3D11 resource to its backing VkImage on DXVK's device.
		 * @param a_resource A D3D11 texture created by the DXVK device.
		 * @param a_outImage Receives the VkImage handle (may be null to skip).
		 * @param a_outLayout Receives the image layout after a flush (may be null).
		 * @param a_outInfo Receives the VkImageCreateInfo (sType must be preset; may be null).
		 * @return true on success.
		 */
	bool GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
		VkImageLayout* a_outLayout = nullptr, VkImageCreateInfo* a_outInfo = nullptr) const;

	/** @brief Flush outstanding D3D11 rendering before foreign Vulkan submits. */
	void FlushRenderingCommands() const;
	/** @brief Block until the Vulkan device is idle (vkDeviceWaitIdle). Used to drain in-flight
	 *  presents/queues before manipulating the swapchain (frame-gen method switch), which the
	 *  Streamline DLSS-G guide §13/§19 mandates to avoid present deadlocks. */
	void WaitDeviceIdle() const;
	/** @brief Lock DXVK's submission queue around a foreign Vulkan submit (drains pending work). */
	void LockSubmissionQueue() const;
	/** @brief Release DXVK's submission queue after a foreign Vulkan submit. */
	void ReleaseSubmissionQueue() const;

	// --- Command-buffer ring for recording Streamline tag/evaluate work ---

	/**
		 * @brief Creates the command pool + per-frame command-buffer/fence ring on
		 *        DXVK's queue family. Required before BeginFrameCommandBuffer().
		 * @param a_framesInFlight Ring depth (number of in-flight command buffers).
		 * @return true on success.
		 */
	bool CreateCommandResources(uint32_t a_framesInFlight = 3);

	/** @brief Destroys the command pool, command buffers and fences. */
	void DestroyCommandResources();

	/**
		 * @brief Waits every in-flight ring submission and flushes deferred view deletes, WITHOUT
		 *        tearing the ring down. Call before destroying interop textures (resolution / upscale-
		 *        method change): the upscaler eval submits work to DXVK's queue asynchronously
		 *        (waitIdle=false), invisible to DXVK's resource-lifetime tracker, so an in-flight
		 *        dispatch could still be writing an interop image that is about to be freed. Draining
		 *        here restores that guarantee at a CPU cost paid ONLY on rare resource-change frames —
		 *        the per-frame eval path stays async.
		 */
	void DrainCommandRing();

	/** @brief Whether the command ring is ready (CreateCommandResources succeeded). */
	bool CommandResourcesReady() const { return commandPool != VK_NULL_HANDLE; }

	/**
		 * @brief Acquires the next ring command buffer (waits its fence), begins it
		 *        (ONE_TIME_SUBMIT) and returns it for ffxGetCommandListVK.
		 * @return The VkCommandBuffer, or VK_NULL_HANDLE on failure.
		 */
	VkCommandBuffer BeginFrameCommandBuffer();

	/**
		 * @brief Ends + submits the current ring command buffer on DXVK's queue.
		 *        Flushes DXVK's pending D3D11 work first, then submits under the
		 *        submission-queue lock with the ring fence. Optionally blocks until done.
		 * @param a_commandBuffer The buffer returned by BeginFrameCommandBuffer().
		 * @param a_waitIdle If true, waits the fence before returning (needed when the
		 *        result is consumed immediately, e.g. presenting an interpolated frame).
		 * @return true on success.
		 */
	bool SubmitFrameCommandBuffer(VkCommandBuffer a_commandBuffer, bool a_waitIdle = false);

	/**
		 * @brief Enqueue transient VkImageViews for destruction once the CURRENT ring slot's
		 *        submission completes (i.e. when that slot is next reused, its fence already
		 *        signalled). Lets a caller that recorded the views into the just-submitted
		 *        command buffer free them without a per-frame vkWaitForFences stall — the views
		 *        stay alive until the GPU is provably done with them.
		 * @param a_views Array of views (VK_NULL_HANDLE entries are ignored).
		 * @param a_count Number of entries in @p a_views.
		 */
	void QueueViewsForDeferredDelete(const VkImageView* a_views, uint32_t a_count);

private:
	DxvkInterop() = default;

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

	// Command-buffer ring (recorded on DXVK's queue family, submitted on its queue).
	// Streamline records from both render and present paths, so a transaction lock must cover the
	// entire BeginFrameCommandBuffer -> record -> SubmitFrameCommandBuffer sequence.
	std::recursive_mutex commandRingMutex;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers;
	std::vector<VkFence> commandFences;
	// Per-slot transient image views awaiting destruction. Freed when the slot's fence signals
	// (at reuse in BeginFrameCommandBuffer), so the DLSS-eval path need not block on the GPU to
	// free the views it recorded. Indexed like commandBuffers/commandFences.
	std::vector<std::vector<VkImageView>> pendingViewDeletes;
	uint32_t framesInFlight = 0;
	uint32_t commandFrameIndex = 0;
};
