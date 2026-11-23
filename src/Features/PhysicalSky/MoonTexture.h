#pragma once

namespace MoonTexture
{
	/**
	 * @brief Mean linear colour times alpha over a moon texture's top mip, decoded as Sky.hlsl's TextureToWorking does.
	 * @param path Texture path as the engine stores it, e.g. "Data/Textures/Sky/Masser_full.dds"; loose or archived.
	 * @param linearLighting Whether Linear Lighting's legacy texture gamma decode applies.
	 * @return The mean in linear sRGB, or nullopt when the texture cannot be read. Cached per path and mode.
	 */
	std::optional<float3> GetMeanColor(std::string_view path, bool linearLighting);
}
