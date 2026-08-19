#include "DXVKPresenterState.h"

#include <algorithm>

void DXVKPresenterState::SetQuery(QueryFn a_query)
{
	std::lock_guard lock(mutex);
	query = a_query;
}

bool DXVKPresenterState::Refresh()
{
	QueryFn currentQuery = nullptr;
	{
		std::lock_guard lock(mutex);
		currentQuery = query;
	}
	if (!currentQuery)
		return false;
	uint32_t format = VK_FORMAT_UNDEFINED;
	uint32_t requested = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	uint32_t effective = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	const uint64_t serial = currentQuery(&format, &requested, &effective);
	if (!serial)
		return false;
	std::lock_guard lock(mutex);
	if (serial <= observed.serial)
		return false;
	observed = { serial, static_cast<VkFormat>(format), static_cast<VkColorSpaceKHR>(requested),
		static_cast<VkColorSpaceKHR>(effective) };
	logger::info("[DXVKInterop] Observed presenter surface serial {}: format={}, requestedColorSpace={}, effectiveColorSpace={}",
		serial, format, requested, effective);
	return true;
}

void DXVKPresenterState::CommitForRenderFrame()
{
	std::lock_guard lock(mutex);
	if (!observed.serial || observed.serial <= committed.serial)
		return;
	if (transitionPending) {
		if (observed.serial <= transitionBaselineSerial || observed.requestedColorSpace != transitionRequestedColorSpace)
			return;
		transitionPending = false;
	}
	committed = observed;
	logger::info("[DXVKInterop] Committed presenter surface serial {} for render frames", committed.serial);
	if (Classify(committed) == Encoding::kHDR10ScRGBFallback)
		logger::warn("[DXVKInterop] HDR frame generation disabled for the scRGB presenter fallback; a HUD-less image rendered directly in the presenter encoding is required");
}

void DXVKPresenterState::BeginTransition(bool a_hdr, bool a_requireNewSerial)
{
	const auto requested = RequestedColorSpace(a_hdr);
	std::lock_guard lock(mutex);
	if (transitionPending && transitionRequestedColorSpace == requested)
		return;
	if (!transitionPending && !a_requireNewSerial && (Matches(committed, requested) || Matches(observed, requested)))
		return;
	transitionPending = true;
	transitionRequestedColorSpace = requested;
	transitionBaselineSerial = std::max(observed.serial, committed.serial);
	logger::info("[DXVKInterop] Presenter color-space transition started: target={}, baselineSerial={}",
		static_cast<uint32_t>(requested), transitionBaselineSerial);
}

void DXVKPresenterState::CancelTransition(bool a_hdr)
{
	const auto requested = RequestedColorSpace(a_hdr);
	std::lock_guard lock(mutex);
	if (transitionPending && transitionRequestedColorSpace == requested) {
		transitionPending = false;
		logger::warn("[DXVKInterop] Presenter color-space transition cancelled for target={}", static_cast<uint32_t>(requested));
	}
}

DXVKPresenterState::Encoding DXVKPresenterState::GetEncodingForFrame() const
{
	std::lock_guard lock(mutex);
	return transitionPending ? Encoding::kUnknown : Classify(committed);
}

VkFormat DXVKPresenterState::GetFormatForFrame() const
{
	std::lock_guard lock(mutex);
	return transitionPending ? VK_FORMAT_UNDEFINED : committed.format;
}

bool DXVKPresenterState::IsReadyForFrame(bool a_hdr) const
{
	std::lock_guard lock(mutex);
	if (transitionPending || !Matches(committed, RequestedColorSpace(a_hdr)))
		return false;
	return Classify(committed) != Encoding::kHDR10ScRGBFallback;
}
