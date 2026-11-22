#pragma once

#include "PhysicalLensDiffraction.h"
#include "PhysicalLensMicrostructure.h"
#include "PhysicalLensSpectrum.h"
#include <array>

// Two-dimensional wave aperture for beam rasterization, including interference
// between edges/corners. The propagation order follows Hullin 2011 equation (5);
// it is a plausible fit, not a measured optical prescription for ghost waves.
namespace PhysicalLensAperture
{
	using PhysicalLensSpectrum::Weights;

	using Complex = PhysicalLensDiffraction::Complex;
	using Pixel = std::array<float, 4>;
	inline constexpr unsigned Size = 512;
	struct Settings
	{
		unsigned blades = 9;
		double curvature = 0, rotation = 0, fNumber = 5.6;
		double surfaceAmount = 0, pupilRadiusMM = 5;
		uint32_t surfaceSeed = 1;
		bool operator==(const Settings&) const = default;
	};
	inline void Fourier2D(std::vector<Complex>& field, unsigned size, bool inverse = false)
	{
		if (inverse)
			for (auto& value : field) value = std::conj(value);
		for (unsigned y = 0; y < size; ++y)
			PhysicalLensDiffraction::Fourier(std::span(field).subspan(y * size, size));
		std::vector<Complex> column(size);
		for (unsigned x = 0; x < size; ++x) {
			for (unsigned y = 0; y < size; ++y) column[y] = field[y * size + x];
			PhysicalLensDiffraction::Fourier(column);
			for (unsigned y = 0; y < size; ++y) field[y * size + x] = column[y];
		}
		if (inverse)
			for (auto& value : field) value = std::conj(value) / double(size * size);
	}
	// Fractional Fourier rotation: chirp / Fresnel propagation / chirp.
	// Coordinate spacing 1/sqrt(N) makes order=1 the Fourier plane. The
	// operator acts on complex amplitude and conserves energy by construction.
	inline void FractionalFourier(std::vector<Complex>& field, unsigned size, double order)
	{
		const double theta = order * std::numbers::pi / 2;
		const double chirp = -std::numbers::pi * std::tan(theta / 2) / size;
		auto multiplyChirp = [&] {
			for (unsigned y = 0; y < size; ++y)
				for (unsigned x = 0; x < size; ++x) {
					double px = double(x) - size * .5, py = double(y) - size * .5;
					field[y * size + x] *= std::polar(1., chirp * (px * px + py * py));
				}
		};
		multiplyChirp();
		Fourier2D(field, size);
		for (unsigned y = 0; y < size; ++y)
			for (unsigned x = 0; x < size; ++x) {
				double u = x < size / 2 ? double(x) : double(x) - size;
				double v = y < size / 2 ? double(y) : double(y) - size;
				field[y * size + x] *= std::polar(1., -std::numbers::pi * std::sin(theta) * (u * u + v * v) / size);
			}
		Fourier2D(field, size, true);
		multiplyChirp();
	}
	inline double Clearance(double x, double y, const Settings& s)
	{
		const double pi = std::numbers::pi, sector = 2 * pi / s.blades;
		double angle = std::atan2(y, x) - s.rotation;
		double delta = angle - sector * std::floor(angle / sector + .5);
		double radius = std::hypot(x, y), px = radius * std::cos(delta), py = radius * std::sin(delta);
		double apothem = std::cos(pi / s.blades), sag = (1 - apothem) * s.curvature;
		double edge = 2 * std::sin(pi / s.blades);
		return 1 - (px + 4 * sag * py * py / (edge * edge)) / (apothem + sag);
	}
	inline std::vector<Pixel> NearField(const Settings& settings)
	{
		std::vector<double> amplitude(Size * Size);
		for (unsigned y = 0; y < Size; ++y)
			for (unsigned x = 0; x < Size; ++x) {
				double value = 0;
				for (unsigned sy = 0; sy < 4; ++sy)
					for (unsigned sx = 0; sx < 4; ++sx)
						value += Clearance((x + (sx + .5) / 4 - Size * .5) * 4 / Size,
									 (y + (sy + .5) / 4 - Size * .5) * 4 / Size, settings) >= 0;
				amplitude[y * Size + x] = value / 16;
			}
		std::vector<Pixel> result(Size * Size);
		std::vector<PhysicalLensMicrostructure::Sample> surface(Size * Size);
		if (settings.surfaceAmount > 0)
			for (unsigned y = 0; y < Size; ++y)
				for (unsigned x = 0; x < Size; ++x)
					surface[y * Size + x] = PhysicalLensMicrostructure::Evaluate(
						(x + .5 - Size * .5) * 4 * settings.pupilRadiusMM / Size,
						(y + .5 - Size * .5) * 4 * settings.pupilRadiusMM / Size,
						4 * settings.pupilRadiusMM / Size, settings.surfaceSeed);
		for (unsigned sample = 0; sample < 31; ++sample) {
			double wavelength = .4 + .01 * sample;
			std::vector<Complex> field(amplitude.begin(), amplitude.end());
			if (settings.surfaceAmount > 0)
				for (unsigned i = 0; i < field.size(); ++i)
					field[i] *= std::polar(std::exp(-settings.surfaceAmount * surface[i].opticalDepth),
						2 * std::numbers::pi * settings.surfaceAmount * surface[i].opticalPathUM / wavelength);
			double order = .15 * (wavelength / .4) * (settings.fNumber / 18);
			FractionalFourier(field, Size, order);
			for (unsigned i = 0; i < field.size(); ++i)
				for (unsigned band = 0; band < 3; ++band)
					result[i][band] += float(std::norm(field[i]) * Weights[sample][band]);
		}
		return result;
	}
}
