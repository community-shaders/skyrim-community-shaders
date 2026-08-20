#pragma once

#include "DXVKInterop.h"
#include "StreamlineSdk.h"

#include <atomic>
#include <array>

/** Converts DXVK-backed D3D11 resources into guarded Streamline Vulkan resources. */
class VulkanResourceBridge
{
public:
	VulkanResourceBridge(VulkanDeviceContext& a_vulkan, std::atomic<bool>& a_dispatchFaulted) :
		vulkan(a_vulkan), dispatchFaulted(a_dispatchFaulted) {}

	[[nodiscard]] PFN_vkVoidFunction GetDeviceProcAddress(const char* a_name) noexcept;
	[[nodiscard]] bool WrapImage(VkDevice a_device, PFN_vkCreateImageView a_createView,
		ID3D11Resource* a_resource, sl::Resource& a_output, sl::SubresourceRange& a_subresource,
		VkImageView& a_outputView, bool& a_terminalFault) noexcept;
	[[nodiscard]] bool DestroyViews(VkDevice a_device, PFN_vkDestroyImageView a_destroyImageView,
		VkImageView* a_views, uint32_t a_count, ID3D11Resource* const* a_resources = nullptr,
		uint32_t a_resourceCount = 0) noexcept;
	[[nodiscard]] bool BarrierUpscalerOutput(
		VkCommandBuffer a_commandBuffer, const sl::Resource& a_output) noexcept;

private:
	VulkanDeviceContext& vulkan;
	std::atomic<bool>& dispatchFaulted;
};

/** Owns one evaluation's temporary views and D3D resource lifetimes. */
class EvaluationResourceTransaction
{
public:
	static constexpr uint32_t kMaxResources = 5;

	EvaluationResourceTransaction(VulkanDeviceContext& a_vulkan, VulkanResourceBridge& a_bridge,
		PFN_vkCreateImageView a_createView, PFN_vkDestroyImageView a_destroyView,
		std::array<ID3D11Resource*, kMaxResources> a_resources);

	[[nodiscard]] bool Wrap(ID3D11Resource* a_resource, sl::Resource& a_output);
	[[nodiscard]] bool Destroy();
	void Quarantine();
	void Defer(const VulkanDeviceContext::CommandTransaction& a_transaction);
	void RetainForFSRPresent(const VulkanDeviceContext::CommandTransaction& a_transaction);

	ID3D11Resource* const* Resources() const { return resources.data(); }
	VkImageView* Views() { return views.data(); }
	const VkImageView* Views() const { return views.data(); }
	uint32_t ViewCount() const { return viewCount; }
	bool HasTerminalFault() const { return terminalFault; }

private:
	VulkanDeviceContext& vulkan;
	VulkanResourceBridge& bridge;
	VkDevice device;
	PFN_vkCreateImageView createView;
	PFN_vkDestroyImageView destroyView;
	std::array<ID3D11Resource*, kMaxResources> resources;
	std::array<VkImageView, kMaxResources> views{};
	std::array<sl::SubresourceRange, kMaxResources> subresources{};
	uint32_t viewCount = 0;
	uint32_t resourceCount = 0;
	bool terminalFault = false;
};
