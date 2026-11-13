#pragma once

#include <DirectXTex.h>
#include <filesystem>

namespace Screenshot
{
	/** @brief Saves PQ/BT.2020 pixels as a lossless 10-bit or 12-bit AVIF image. */
	bool SaveHdrAvif(const DirectX::Image& source, const std::filesystem::path& outputPath);
}
