#include "VulkanResourceBridge.h"

namespace
{
	VkImageAspectFlags ImageAspect(VkFormat a_format)
	{
		switch (a_format) {
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		default:
			return VK_IMAGE_ASPECT_COLOR_BIT;
		}
	}
}

PFN_vkVoidFunction VulkanResourceBridge::GetDeviceProcAddress(const char* a_name) noexcept
{
	PFN_vkVoidFunction function = nullptr;
	__try {
		if (const auto getProcAddr = vulkan.GetDeviceProcAddr())
			function = getProcAddr(vulkan.GetDevice(), a_name);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		dispatchFaulted.store(true, std::memory_order_release);
	}
	return function;
}

bool VulkanResourceBridge::WrapImage(VkDevice a_device, PFN_vkCreateImageView a_createView,
	ID3D11Resource* a_resource, sl::Resource& a_output, sl::SubresourceRange& a_subresource,
	VkImageView& a_outputView, bool& a_terminalFault) noexcept
{
	a_outputView = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	bool mapped = false;
	DWORD exceptionCode = 0;
	__try {
		mapped = vulkan.GetVkImage(a_resource, &image, &layout, &info);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		exceptionCode = GetExceptionCode();
	}
	if (exceptionCode) {
		a_terminalFault = true;
		vulkan.QuarantineResourcesAfterVulkanDestructionFault(&a_resource, 1);
		dispatchFaulted.store(true, std::memory_order_release);
		logger::error("[Streamline] DXVK image interop faulted (SEH {:#x})", exceptionCode);
		return false;
	}
	if (!mapped || image == VK_NULL_HANDLE || !a_createView)
		return false;

	VkImageViewCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	createInfo.image = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = info.format;
	createInfo.subresourceRange.aspectMask = ImageAspect(info.format);
	createInfo.subresourceRange.levelCount = 1;
	createInfo.subresourceRange.layerCount = 1;
	VkResult result = VK_ERROR_DEVICE_LOST;
	__try {
		result = a_createView(a_device, &createInfo, nullptr, &a_outputView);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		exceptionCode = GetExceptionCode();
	}
	if (exceptionCode || result != VK_SUCCESS || a_outputView == VK_NULL_HANDLE) {
		if (exceptionCode || a_outputView != VK_NULL_HANDLE) {
			a_terminalFault = true;
			vulkan.QuarantineResourcesAfterVulkanDestructionFault(&a_resource, 1);
		}
		a_outputView = VK_NULL_HANDLE;
		dispatchFaulted.store(true, std::memory_order_release);
		logger::error("[Streamline] Vulkan image-view creation failed (result {}, SEH {:#x})",
			static_cast<int>(result), exceptionCode);
		return false;
	}

	a_output = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, a_outputView, static_cast<uint32_t>(layout) };
	a_output.width = info.extent.width;
	a_output.height = info.extent.height;
	a_output.nativeFormat = static_cast<uint32_t>(info.format);
	a_output.mipLevels = info.mipLevels;
	a_output.arrayLayers = info.arrayLayers;
	a_output.usage = static_cast<uint32_t>(info.usage);
	a_output.flags = static_cast<uint32_t>(info.flags);
	a_subresource.aspectMask = createInfo.subresourceRange.aspectMask;
	a_subresource.baseMipLevel = 0;
	a_subresource.levelCount = 1;
	a_subresource.baseArrayLayer = 0;
	a_subresource.layerCount = 1;
	a_output.next = &a_subresource;
	return true;
}

bool VulkanResourceBridge::DestroyViews(VkDevice a_device, PFN_vkDestroyImageView a_destroyImageView,
	VkImageView* a_views, uint32_t a_count, ID3D11Resource* const* a_resources,
	uint32_t a_resourceCount) noexcept
{
	if (!a_destroyImageView)
		return false;
	for (uint32_t i = 0; i < a_count; ++i) {
		if (a_views[i] == VK_NULL_HANDLE)
			continue;
		bool completed = false;
		__try {
			a_destroyImageView(a_device, a_views[i], nullptr);
			completed = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		a_views[i] = VK_NULL_HANDLE;
		if (!completed) {
			dispatchFaulted.store(true, std::memory_order_release);
			vulkan.QuarantineResourcesAfterVulkanDestructionFault(a_resources, a_resourceCount);
			return false;
		}
	}
	return true;
}

bool VulkanResourceBridge::BarrierUpscalerOutput(
	VkCommandBuffer a_commandBuffer, const sl::Resource& a_output) noexcept
{
	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.oldLayout = static_cast<VkImageLayout>(a_output.state);
	barrier.newLayout = static_cast<VkImageLayout>(a_output.state);
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = static_cast<VkImage>(a_output.native);
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = a_output.mipLevels;
	barrier.subresourceRange.layerCount = a_output.arrayLayers;
	bool completed = false;
	__try {
		vkCmdPipelineBarrier(a_commandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
			0, nullptr, 0, nullptr, 1, &barrier);
		completed = true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
	if (!completed)
		dispatchFaulted.store(true, std::memory_order_release);
	return completed;
}

EvaluationResourceTransaction::EvaluationResourceTransaction(VulkanDeviceContext& a_vulkan,
	VulkanResourceBridge& a_bridge, PFN_vkCreateImageView a_createView,
	PFN_vkDestroyImageView a_destroyView,
	std::array<ID3D11Resource*, kMaxResources> a_resources) :
	vulkan(a_vulkan), bridge(a_bridge), device(a_vulkan.GetDevice()),
	createView(a_createView), destroyView(a_destroyView), resources(a_resources) {}

bool EvaluationResourceTransaction::Wrap(ID3D11Resource* a_resource, sl::Resource& a_output)
{
	if (resourceCount >= kMaxResources || viewCount >= kMaxResources)
		return false;
	VkImageView view = VK_NULL_HANDLE;
	const bool wrapped = bridge.WrapImage(device, createView, a_resource, a_output,
		subresources[resourceCount], view, terminalFault);
	if (view != VK_NULL_HANDLE)
		views[viewCount++] = view;
	if (wrapped)
		++resourceCount;
	return wrapped;
}

bool EvaluationResourceTransaction::Destroy()
{
	return bridge.DestroyViews(device, destroyView, views.data(), viewCount,
		resources.data(), static_cast<uint32_t>(resources.size()));
}

void EvaluationResourceTransaction::Quarantine()
{
	vulkan.QuarantineResourcesAfterVulkanDestructionFault(
		resources.data(), static_cast<uint32_t>(resources.size()));
	views.fill(VK_NULL_HANDLE);
}

void EvaluationResourceTransaction::Defer(const VulkanDeviceContext::CommandTransaction& a_transaction)
{
	vulkan.QueueResourcesForDeferredRelease(
		a_transaction, resources.data(), static_cast<uint32_t>(resources.size()));
	vulkan.QueueViewsForDeferredDelete(a_transaction, views.data(), viewCount);
}

void EvaluationResourceTransaction::RetainForFSRPresent(
	const VulkanDeviceContext::CommandTransaction& a_transaction)
{
	vulkan.QueueResourcesForPresent(
		a_transaction, resources.data(), static_cast<uint32_t>(resources.size()));
	vulkan.QueueViewsForFSRPresent(a_transaction, views.data(), viewCount);
}
