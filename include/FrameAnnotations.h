#pragma once

#include <cstdint>

namespace FrameAnnotations
{
	void OnPostPostLoad();
	void OnDataLoaded();
	bool IsInRenderShadowmasksPhase();

	/**
	 * @brief Were the ImageSpace target wrappers actually installed this session?
	 *
	 * CDO4-001 phase 2, item 4. This is not a convenience: `OnPostPostLoad`
	 * returns early when `frameAnnotations` is false, and EVERY ImageSpace vtable
	 * write - including the three passes the dynamic-resolution replacement hooks
	 * - sits after that return. So with Frame Annotations off the wrappers do not
	 * exist, and an absent pass trace means "never installed" rather than "never
	 * called".
	 *
	 * The public MGO presets set Frame Annotations false, which makes this the
	 * difference between a null result and a meaningless one.
	 */
	[[nodiscard]] bool TargetWrappersInstalled() noexcept;

	/** @brief Non-zero once the wrappers are installed; identifies the install. */
	[[nodiscard]] std::uint32_t TargetWrapperInstallId() noexcept;
}
