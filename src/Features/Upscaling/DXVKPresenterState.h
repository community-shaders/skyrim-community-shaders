#pragma once

#include <cstdint>
#include <mutex>
#include <vulkan/vulkan.h>

class DXVKPresenterState
{
public:
	enum class Encoding : uint8_t
	{
		kUnknown,
		kSDR,
		kHDR10,
		kHDR10ScRGBFallback,
	};

	using QueryFn = uint64_t (*)(uint32_t*, uint32_t*, uint32_t*);

	void SetQuery(QueryFn a_query);
	bool Refresh();
	void CommitForRenderFrame();
	void BeginTransition(bool a_hdr, bool a_requireNewSerial = false);
	void CancelTransition(bool a_hdr);
	[[nodiscard]] Encoding GetEncodingForFrame() const;
	[[nodiscard]] VkFormat GetFormatForFrame() const;
	[[nodiscard]] bool IsReadyForFrame(bool a_hdr) const;

	struct SurfaceState
	{
		uint64_t serial = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkColorSpaceKHR requestedColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkColorSpaceKHR effectiveColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	};

	static constexpr VkColorSpaceKHR RequestedColorSpace(bool a_hdr)
	{
		return a_hdr ? VK_COLOR_SPACE_HDR10_ST2084_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}
	static constexpr Encoding Classify(const SurfaceState& a_state)
	{
		if (!a_state.serial)
			return Encoding::kUnknown;
		if (a_state.requestedColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
			a_state.effectiveColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return Encoding::kSDR;
		if (a_state.requestedColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
			if (a_state.effectiveColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
				return Encoding::kHDR10;
			if (a_state.effectiveColorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
				return Encoding::kHDR10ScRGBFallback;
		}
		return Encoding::kUnknown;
	}
	static constexpr bool Matches(const SurfaceState& a_state, VkColorSpaceKHR a_requestedColorSpace)
	{
		if (!a_state.serial || a_state.requestedColorSpace != a_requestedColorSpace)
			return false;
		const Encoding encoding = Classify(a_state);
		if (a_requestedColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
			return encoding == Encoding::kHDR10 || encoding == Encoding::kHDR10ScRGBFallback;
		return a_requestedColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && encoding == Encoding::kSDR;
	}

private:
	mutable std::mutex mutex;
	QueryFn query = nullptr;
	SurfaceState observed;
	SurfaceState committed;
	bool transitionPending = false;
	uint64_t transitionBaselineSerial = 0;
	VkColorSpaceKHR transitionRequestedColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
};
