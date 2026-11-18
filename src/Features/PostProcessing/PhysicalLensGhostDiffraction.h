#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Local straight-edge Fresnel response, spectrally integrated and convolved
// with a Gaussian emitter/pixel footprint. Hullin et al. 2011, section 3.3,
// motivates near-field ghost diffraction; this is a local edge approximation,
// not their FrFT aperture texture or a full wave solution at corners/caustics.
namespace PhysicalLensGhostDiffraction
{
	inline constexpr unsigned Width = 512, Height = 48;
	using Pixel = std::array<float, 4>;
	/// Cached dimensionless residual relative to a geometrical Gaussian edge.
	inline const std::vector<Pixel>& Table()
	{
		static const auto table = [] {
			constexpr double pi = 3.14159265358979323846;
			// Integrate the Fresnel functions once, then interpolate. Resolves the
			// fastest phase at the outermost lookup to better than 0.1 radians.
			constexpr unsigned count = 65536;
			constexpr double step = 32.0 / count;
			std::vector<std::array<double, 2>> cs(count + 1);
			for (unsigned i = 1; i <= count; ++i) {
				double a = (i - 1) * step, b = i * step, m = (a + b) * .5;
				cs[i] = { cs[i - 1][0] + step / 6 * (std::cos(pi * a * a / 2) + 4 * std::cos(pi * m * m / 2) + std::cos(pi * b * b / 2)),
					cs[i - 1][1] + step / 6 * (std::sin(pi * a * a / 2) + 4 * std::sin(pi * m * m / 2) + std::sin(pi * b * b / 2)) };
			}
			auto intensity = [&](double x) {
				double index = std::min(std::abs(x) / step, double(count - 1));
				unsigned i = unsigned(index);
				double c = .5 + std::copysign(std::lerp(cs[i][0], cs[i + 1][0], index - i), x);
				double s = .5 + std::copysign(std::lerp(cs[i][1], cs[i + 1][1], index - i), x);
				return .5 * (c * c + s * s);
			};
			constexpr unsigned samples = 16384;
			std::vector<Pixel> spectrum(samples + 1);
			for (unsigned i = 0; i <= samples; ++i) {
				double x = -26 + 56.0 * i / samples;
				for (unsigned band = 0; band < 3; ++band) {
					double center = .65 - .1 * band, value = 0;
					// Finite 100 nm bands, not three monochromatic diffraction patterns.
					for (unsigned w = 0; w < 64; ++w)
						value += intensity(x * std::sqrt(center / (center + ((w + .5) / 64 - .5) * .1)));
					spectrum[i][band] = float(value / 64);
				}
			}
			auto sample = [&](double x, unsigned band) {
				double index = std::clamp((x + 26) * samples / 56, 0., double(samples - 1));
				unsigned i = unsigned(index);
				return std::lerp(double(spectrum[i][band]), double(spectrum[i + 1][band]), index - i);
			};
			std::vector<Pixel> result(Width * Height);
			for (unsigned y = 0; y < Height; ++y) {
				double sigma = 4 * std::pow(double(y) / (Height - 1), 2);
				for (unsigned x = 0; x < Width; ++x) {
					double u = -6 + 16.0 * x / (Width - 1);
					double geometric = sigma > 0 ? .5 * std::erfc(-u / (std::sqrt(2.) * sigma)) : (u >= 0 ? 1 : 0);
					for (unsigned band = 0; band < 3; ++band) {
						double sum = 0, weights = 0;
						for (unsigned k = 0; k < 128; ++k) {
							double q = ((k + .5) / 128 - .5) * 10;
							double weight = std::exp(-q * q / 2);
							sum += weight * sample(u + sigma * q, band);
							weights += weight;
						}
						// Fade the weak asymptotic residual, not the ghost image. Large
						// emitters smoothly return to the existing geometric response.
						auto smooth = [](double v) { v = std::clamp(v, 0., 1.); return v * v * (3 - 2 * v); };
						double support = smooth(u + 6) * smooth((10 - u) / 2) * smooth(4 - sigma);
						result[y * Width + x][band] = float((sum / weights - geometric) * support);
					}
				}
			}
			return result;
		}();
		return table;
	}
}
