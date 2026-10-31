#include "Avif.h"

#include <DirectXPackedVector.h>
#include <algorithm>
#include <avif/avif.h>
#include <cmath>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

namespace
{
	struct AvifOutput
	{
		avifRWData data = AVIF_DATA_EMPTY;
		~AvifOutput() { avifRWDataFree(&data); }
	};

	double PQToNits(double pq)
	{
		constexpr double m1 = 2610.0 / 16384.0;
		constexpr double m2 = 2523.0 / 32.0;
		constexpr double c1 = 3424.0 / 4096.0;
		constexpr double c2 = 2413.0 / 128.0;
		constexpr double c3 = 2392.0 / 128.0;
		const double p = std::pow(pq, 1.0 / m2);
		return 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
	}
}

bool Screenshot::SaveHdrAvif(const DirectX::Image& source, const std::filesystem::path& outputPath)
{
	const bool packed10 = source.format == DXGI_FORMAT_R10G10B10A2_UNORM;
	const bool float16 = source.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
	if ((!packed10 && !float16) || !source.pixels || source.width == 0 || source.height == 0 ||
		source.width > UINT32_MAX || source.height > UINT32_MAX ||
		source.rowPitch < source.width * (packed10 ? 4 : 8) || source.slicePitch / source.rowPitch < source.height)
		return false;

	const uint32_t depth = packed10 ? 10 : 12;
	const uint32_t maxSample = (1u << depth) - 1;
	std::unique_ptr<avifImage, decltype(&avifImageDestroy)> image(
		avifImageCreate(static_cast<uint32_t>(source.width), static_cast<uint32_t>(source.height), depth, AVIF_PIXEL_FORMAT_YUV444), avifImageDestroy);
	if (!image)
		return false;

	image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2020;
	image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
	image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
	image->yuvRange = AVIF_RANGE_FULL;
	const auto allocateResult = avifImageAllocatePlanes(image.get(), AVIF_PLANES_YUV);
	if (allocateResult != AVIF_RESULT_OK) {
		logger::error("AVIF plane allocation failed: {}", avifResultToString(allocateResult));
		return false;
	}

	std::vector<double> lightLevels(maxSample + 1);
	for (uint32_t sample = 0; sample <= maxSample; ++sample)
		lightLevels[sample] = PQToNits(static_cast<double>(sample) / maxSample);

	double totalLight = 0.0;
	uint16_t maxComponent = 0;
	for (size_t y = 0; y < source.height; ++y) {
		const auto* sourceRow = source.pixels + y * source.rowPitch;
		auto* green = reinterpret_cast<uint16_t*>(image->yuvPlanes[AVIF_CHAN_Y] + y * image->yuvRowBytes[AVIF_CHAN_Y]);
		auto* blue = reinterpret_cast<uint16_t*>(image->yuvPlanes[AVIF_CHAN_U] + y * image->yuvRowBytes[AVIF_CHAN_U]);
		auto* red = reinterpret_cast<uint16_t*>(image->yuvPlanes[AVIF_CHAN_V] + y * image->yuvRowBytes[AVIF_CHAN_V]);
		for (size_t x = 0; x < source.width; ++x) {
			uint16_t rgb[3]{};
			if (packed10) {
				const uint32_t pixel = reinterpret_cast<const uint32_t*>(sourceRow)[x];
				rgb[0] = static_cast<uint16_t>(pixel & 1023u);
				rgb[1] = static_cast<uint16_t>((pixel >> 10) & 1023u);
				rgb[2] = static_cast<uint16_t>((pixel >> 20) & 1023u);
			} else {
				const auto* pixel = reinterpret_cast<const DirectX::PackedVector::HALF*>(sourceRow) + x * 4;
				for (size_t channel = 0; channel < 3; ++channel) {
					const float pq = DirectX::PackedVector::XMConvertHalfToFloat(pixel[channel]);
					if (!std::isfinite(pq)) {
						logger::error("AVIF capture contains non-finite PQ pixels.");
						return false;
					}
					rgb[channel] = static_cast<uint16_t>(std::lround(std::clamp(pq, 0.0f, 1.0f) * maxSample));
				}
			}
			red[x] = rgb[0];
			green[x] = rgb[1];
			blue[x] = rgb[2];
			const uint16_t component = std::max({ rgb[0], rgb[1], rgb[2] });
			maxComponent = std::max(maxComponent, component);
			totalLight += lightLevels[component];
		}
	}
	image->clli.maxCLL = static_cast<uint16_t>(std::ceil(std::min(lightLevels[maxComponent], 10000.0)));
	image->clli.maxPALL = static_cast<uint16_t>(std::ceil(std::min(totalLight / (source.width * source.height), lightLevels[maxComponent])));

	std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)> encoder(avifEncoderCreate(), avifEncoderDestroy);
	if (!encoder)
		return false;
	encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
	encoder->quality = AVIF_QUALITY_LOSSLESS;
	encoder->speed = 8;
	encoder->maxThreads = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u));
	encoder->autoTiling = AVIF_TRUE;

	AvifOutput output;
	const auto result = avifEncoderWrite(encoder.get(), image.get(), &output.data);
	if (result != AVIF_RESULT_OK) {
		logger::error("AVIF encoding failed: {} ({})", avifResultToString(result), encoder->diag.error);
		return false;
	}

	std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<const char*>(output.data.data), static_cast<std::streamsize>(output.data.size));
	file.close();
	if (!file) {
		std::error_code ec;
		std::filesystem::remove(outputPath, ec);
		return false;
	}
	logger::info("Community Shaders HDR AVIF: libavif {}, AOM, {}-bit lossless 4:4:4", avifVersion(), depth);
	return true;
}
