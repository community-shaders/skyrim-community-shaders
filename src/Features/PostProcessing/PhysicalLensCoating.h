#pragma once
#include "PhysicalLensDispersion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <mutex>
#include <numbers>
#include <tuple>

// Synthetic lossless two-layer AR stacks, NOT proprietary manufacturer data.
// Characteristic matrices (s/p admittances) preserve reciprocity and R+T=1.
// See Edmund Optics, "An Introduction to Optical Coatings". Bake the expensive
// interference calculation once per glass/interface; tracing uses four reads.
// https://www.edmundoptics.com/knowledge-center/application-notes/lasers/an-introduction-to-optical-coatings/
namespace PhysicalLensCoating
{
	using Table = std::array<std::array<float, 4>, 72>;  // 24 air cosines x 12 wavelengths
	inline double Random(unsigned seed)
	{
		seed ^= seed >> 16;
		seed *= 0x7feb352du;
		seed ^= seed >> 15;
		seed *= 0x846ca68bu;
		seed ^= seed >> 16;
		return double(seed & 0xffffffu) / 16777216.;
	}
	inline double Exact(double glass, double airCosine, double wavelength, unsigned seed, double designGlass = 0)
	{
		if (airCosine <= 0)
			return 1;
		const double design = .44 + .22 * Random(seed + 11);
		const double n[]{ 1.38, 1.38 * std::sqrt(designGlass > 0 ? designGlass : glass) };
		const double thickness[]{ design / (4 * n[0]) * (.96 + .08 * Random(seed + 23)),
			design / (4 * n[1]) * (.96 + .08 * Random(seed + 47)) };
		const double sin2 = 1 - airCosine * airCosine;
		double result = 0;
		for (unsigned polarization = 0; polarization < 2; ++polarization) {
			using C = std::complex<double>;
			C a = 1, b = 0, c = 0, d = 1;
			for (unsigned layer = 0; layer < 2; ++layer) {
				double cosine = std::sqrt(1 - sin2 / (n[layer] * n[layer]));
				double eta = polarization == 0 ? n[layer] * cosine : n[layer] / cosine;
				double phase = 2 * std::numbers::pi * n[layer] * thickness[layer] * cosine / wavelength;
				double cs = std::cos(phase);
				C sn(0, std::sin(phase));
				C aa = a * cs + b * sn * eta, bb = a * sn / eta + b * cs;
				C cc = c * cs + d * sn * eta, dd = c * sn / eta + d * cs;
				a = aa;
				b = bb;
				c = cc;
				d = dd;
			}
			double transmittedCosine = std::sqrt(1 - sin2 / (glass * glass));
			double incident = polarization == 0 ? airCosine : 1 / airCosine;
			double substrate = polarization == 0 ? glass * transmittedCosine : glass / transmittedCosine;
			C numerator = incident * (a + b * substrate) - (c + d * substrate);
			C denominator = incident * (a + b * substrate) + (c + d * substrate);
			result += .5 * std::norm(numerator / denominator);
		}
		return std::clamp(result, 0., 1.);
	}
	inline const Table& Get(double nd, double dispersion, unsigned seed)
	{
		static std::map<std::tuple<double, double, unsigned>, Table> cache;
		static std::mutex mutex;
		std::lock_guard lock(mutex);
		auto [it, inserted] = cache.try_emplace({ nd, dispersion, seed });
		if (inserted)
			for (unsigned angle = 0; angle < 24; ++angle)
				for (unsigned band = 0; band < 12; ++band) {
					double wavelength = .4 + .3 * band / 11;
					double glass = PhysicalLensDispersion::Index(nd, dispersion, wavelength);
					it->second[angle * 3 + band / 4][band % 4] = float(Exact(glass, angle / 23., wavelength, seed, nd));
				}
		return it->second;
	}
	inline double Sample(const Table& table, double airCosine, double wavelength)
	{
		double a = std::clamp(airCosine, 0., 1.) * 23, w = std::clamp((wavelength - .4) * (11 / .3), 0., 11.);
		unsigned ia = std::min(unsigned(a), 22u), iw = std::min(unsigned(w), 10u);
		const auto value = [&](unsigned row, unsigned col) { return table[row * 3 + col / 4][col % 4]; };
		return std::lerp(std::lerp(double(value(ia, iw)), double(value(ia, iw + 1)), w - iw),
			std::lerp(double(value(ia + 1, iw)), double(value(ia + 1, iw + 1)), w - iw), a - ia);
	}
}
