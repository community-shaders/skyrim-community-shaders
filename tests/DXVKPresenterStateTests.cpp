#include <catch2/catch_test_macros.hpp>

#include "Features/Upscaling/DXVKPresenterState.h"
#include "Features/Upscaling/FrameGenWatchdog.h"

namespace
{
	DXVKPresenterState::SurfaceState g_queryState;

	uint64_t QuerySurface(uint32_t* a_format, uint32_t* a_requested, uint32_t* a_effective)
	{
		*a_format = static_cast<uint32_t>(g_queryState.format);
		*a_requested = static_cast<uint32_t>(g_queryState.requestedColorSpace);
		*a_effective = static_cast<uint32_t>(g_queryState.effectiveColorSpace);
		return g_queryState.serial;
	}

	void SetQueryState(uint64_t a_serial, VkFormat a_format, VkColorSpaceKHR a_requested, VkColorSpaceKHR a_effective)
	{
		g_queryState = { a_serial, a_format, a_requested, a_effective };
	}
}

TEST_CASE("DXVK presenter commits monotonically and ignores repeated observations")
{
	DXVKPresenterState state;
	state.SetQuery(QuerySurface);

	SetQueryState(1, VK_FORMAT_B8G8R8A8_UNORM,
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	CHECK(state.Refresh());
	state.CommitForRenderFrame();
	CHECK(state.IsReadyForFrame(false));
	CHECK(state.GetEncodingForFrame() == DXVKPresenterState::Encoding::kSDR);
	CHECK_FALSE(state.Refresh());

	SetQueryState(0, VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		VK_COLOR_SPACE_HDR10_ST2084_EXT, VK_COLOR_SPACE_HDR10_ST2084_EXT);
	CHECK_FALSE(state.Refresh());
	CHECK(state.IsReadyForFrame(false));
}

TEST_CASE("DXVK presenter transition requires a newer matching surface")
{
	DXVKPresenterState state;
	state.SetQuery(QuerySurface);
	SetQueryState(5, VK_FORMAT_B8G8R8A8_UNORM,
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	REQUIRE(state.Refresh());
	state.CommitForRenderFrame();

	state.BeginTransition(true, true);
	CHECK_FALSE(state.IsReadyForFrame(false));
	CHECK_FALSE(state.IsReadyForFrame(true));
	CHECK(state.GetEncodingForFrame() == DXVKPresenterState::Encoding::kUnknown);

	SetQueryState(6, VK_FORMAT_B8G8R8A8_UNORM,
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	REQUIRE(state.Refresh());
	state.CommitForRenderFrame();
	CHECK_FALSE(state.IsReadyForFrame(false));

	SetQueryState(7, VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		VK_COLOR_SPACE_HDR10_ST2084_EXT, VK_COLOR_SPACE_HDR10_ST2084_EXT);
	REQUIRE(state.Refresh());
	state.CommitForRenderFrame();
	CHECK(state.IsReadyForFrame(true));
	CHECK(state.GetEncodingForFrame() == DXVKPresenterState::Encoding::kHDR10);
}

TEST_CASE("DXVK presenter transition cancellation restores the committed frame state")
{
	DXVKPresenterState state;
	state.SetQuery(QuerySurface);
	SetQueryState(10, VK_FORMAT_B8G8R8A8_UNORM,
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	REQUIRE(state.Refresh());
	state.CommitForRenderFrame();

	state.BeginTransition(true);
	CHECK(state.GetEncodingForFrame() == DXVKPresenterState::Encoding::kUnknown);
	state.CancelTransition(true);
	CHECK(state.IsReadyForFrame(false));
	CHECK(state.GetEncodingForFrame() == DXVKPresenterState::Encoding::kSDR);
}

TEST_CASE("scRGB fallback matches HDR but is not frame-generation ready")
{
	DXVKPresenterState state;
	state.SetQuery(QuerySurface);
	SetQueryState(12, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_COLOR_SPACE_HDR10_ST2084_EXT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
	REQUIRE(state.Refresh());
	state.CommitForRenderFrame();

	CHECK(state.GetEncodingForFrame() == DXVKPresenterState::Encoding::kHDR10ScRGBFallback);
	CHECK_FALSE(state.IsReadyForFrame(true));
}

TEST_CASE("frame generation stall detector requires both stale heartbeats in the foreground")
{
	FrameGenStallDetector detector;
	constexpr uint64_t now = FrameGenStallDetector::kTimeoutNs * 2;
	constexpr uint64_t stale = 1;
	constexpr uint64_t healthy = now - 1;

	CHECK_FALSE(detector.Poll(false, stale, stale, now, true));
	CHECK_FALSE(detector.Poll(true, 0, stale, now, true));
	CHECK_FALSE(detector.Poll(true, stale, 0, now, true));
	CHECK_FALSE(detector.Poll(true, healthy, stale, now, true));
	CHECK_FALSE(detector.Poll(true, stale, healthy, now, true));
	CHECK_FALSE(detector.Poll(true, stale, stale, now, false));
	CHECK(detector.Poll(true, stale, stale, now, true));
}

TEST_CASE("frame generation stall detector triggers once until disabled")
{
	FrameGenStallDetector detector;
	constexpr uint64_t now = FrameGenStallDetector::kTimeoutNs * 2;

	CHECK(detector.Poll(true, 1, 1, now, true));
	CHECK_FALSE(detector.Poll(true, 1, 1, now + 1, true));
	CHECK_FALSE(detector.Poll(false, 1, 1, now + 2, true));
	CHECK(detector.Poll(true, 1, 1, now + 3, true));
}
