#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace RainRefractionTexture
{
	struct Footprint
	{
		std::array<float, 4> uvBounds{ 0, 0, 1, 1 };
		std::array<float, 2> radiusScale{ 1, 1 };  ///< Billboard half-extents relative to the covered width, so the drop keeps its aspect
	};

	/** @brief Fits a billboard to RGBA8 alpha coverage, keeping a one-texel filter border. */
	inline Footprint GetFootprint(std::span<const uint8_t> a_pixels, size_t a_width, size_t a_height, size_t a_rowPitch)
	{
		constexpr size_t kPixelBytes = 4;
		constexpr size_t kAlphaChannel = 3;
		constexpr size_t kFilterBorder = 1;
		if (!a_width || !a_height || !a_rowPitch || a_width > a_rowPitch / kPixelBytes || a_height > a_pixels.size() / a_rowPitch)
			return {};

		size_t left = a_width, top = a_height, right = 0, bottom = 0;
		for (size_t y = 0; y < a_height; ++y) {
			for (size_t x = 0; x < a_width; ++x) {
				if (!a_pixels[y * a_rowPitch + x * kPixelBytes + kAlphaChannel])
					continue;
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x + 1);
				bottom = std::max(bottom, y + 1);
			}
		}
		if (left >= right)
			return {};

		const auto coverageWidth = static_cast<float>(right - left);
		left -= std::min(left, kFilterBorder);
		top -= std::min(top, kFilterBorder);
		right += std::min(a_width - right, kFilterBorder);
		bottom += std::min(a_height - bottom, kFilterBorder);
		const auto imageWidth = static_cast<float>(a_width);
		const auto imageHeight = static_cast<float>(a_height);
		return {
			.uvBounds = { static_cast<float>(left) / imageWidth, static_cast<float>(top) / imageHeight,
				static_cast<float>(right) / imageWidth, static_cast<float>(bottom) / imageHeight },
			.radiusScale = { static_cast<float>(right - left) / coverageWidth, static_cast<float>(bottom - top) / coverageWidth }
		};
	}
}
