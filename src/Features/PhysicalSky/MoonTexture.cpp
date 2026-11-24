#include "MoonTexture.h"

#include <DirectXTex.h>

namespace
{
	/** @brief Matches ColorManagement::LEGACY_TEXTURE_GAMMA, the decode TextureToWorking applies under Linear Lighting. */
	constexpr float kLegacyTextureGamma = 1.8f;
	constexpr std::string_view kDataPrefix = "data\\";
	constexpr auto kDecodeFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

	/** @brief Lowercase, backslashed path relative to Data, as BSResourceNiBinaryStream expects. */
	std::string ResourcePath(std::string_view path)
	{
		std::string result(path);
		std::ranges::transform(result, result.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
		if (result.starts_with(kDataPrefix))
			result.erase(0, kDataPrefix.size());
		return result;
	}

	/** @brief Decodes the texture's top mip and averages its colour weighted by alpha, as the disc is alpha blended. */
	std::optional<float3> Measure(const std::string& resourcePath, bool linearLighting)
	{
		RE::BSResourceNiBinaryStream stream(resourcePath);
		if (!stream.good())
			return std::nullopt;
		std::vector<uint8_t> bytes(stream.stream->totalSize);
		if (!stream.read(bytes.data(), static_cast<uint32_t>(bytes.size())))
			return std::nullopt;

		DirectX::ScratchImage source, decoded;
		if (FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, source)))
			return std::nullopt;
		const auto& top = *source.GetImage(0, 0, 0);
		const HRESULT decodeResult = DirectX::IsCompressed(top.format) ? DirectX::Decompress(top, kDecodeFormat, decoded) :
		                                                                 DirectX::Convert(top, kDecodeFormat, DirectX::TEX_FILTER_DEFAULT, 0.f, decoded);
		if (FAILED(decodeResult))
			return std::nullopt;

		const auto& image = *decoded.GetImage(0, 0, 0);
		std::array<double, 3> sum{};
		for (size_t y = 0; y < image.height; ++y) {
			const auto* row = reinterpret_cast<const float*>(image.pixels + y * image.rowPitch);
			for (size_t x = 0; x < image.width; ++x) {
				const float* texel = row + x * 4;
				for (size_t channel = 0; channel < sum.size(); ++channel) {
					const float value = std::max(texel[channel], 0.f);
					sum[channel] += (linearLighting ? std::pow(value, kLegacyTextureGamma) : value) * texel[3];
				}
			}
		}
		const double texelCount = static_cast<double>(image.width * image.height);
		return float3(static_cast<float>(sum[0] / texelCount), static_cast<float>(sum[1] / texelCount), static_cast<float>(sum[2] / texelCount));
	}
}

std::optional<float3> MoonTexture::GetMeanColor(std::string_view path, bool linearLighting)
{
	if (path.empty())
		return std::nullopt;

	static std::array<std::unordered_map<std::string, std::optional<float3>>, 2> caches;
	auto& cache = caches[linearLighting];
	auto resourcePath = ResourcePath(path);
	if (const auto found = cache.find(resourcePath); found != cache.end())
		return found->second;

	const auto mean = Measure(resourcePath, linearLighting);
	if (!mean)
		logger::warn("[Physical Sky] Cannot read moon texture '{}'; its disc falls back to vanilla brightness", resourcePath);
	return cache.emplace(std::move(resourcePath), mean).first->second;
}
