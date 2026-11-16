#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

// Procedural equivalent-pupil contamination, not measured manufacturer data.
// Spatial coordinates are millimetres and optical path is micrometres. Stable
// seeded geometry is cropped by a changing iris, rather than resized with it.
namespace PhysicalLensMicrostructure
{
	struct Sample
	{
		double opticalDepth = 0, opticalPathUM = 0;
	};
	inline double Random(uint32_t key)
	{
		key ^= key >> 16;
		key *= 0x7feb352d;
		key ^= key >> 15;
		key *= 0x846ca68b;
		key ^= key >> 16;
		return (key & 0xffffff) / 16777216.;
	}
	inline Sample Evaluate(double x, double y, double pixelWidthMM, uint32_t seed)
	{
		Sample result;
		const double pi = std::numbers::pi;
		const double footprint = pixelWidthMM * pixelWidthMM / 12;
		seed = seed * 173 + 119;
		// Dense fine contamination has a fixed density per square millimetre.
		// A sparse list of large particles alone cannot produce the small-scale
		// interference seen around a bright light. Only neighboring cells matter.
		int cellX = int(std::floor(x / .3)), cellY = int(std::floor(y / .3));
		for (int cy = cellY - 1; cy <= cellY + 1; ++cy)
			for (int cx = cellX - 1; cx <= cellX + 1; ++cx) {
				uint32_t key = uint32_t(cx) * 73856093u ^ uint32_t(cy) * 19349663u ^ (seed + 977u);
				double dx = x - (cx + Random(key)) * .3, dy = y - (cy + Random(key + 1)) * .3;
				double radius = .015 + .025 * Random(key + 2), variance = radius * radius + footprint;
				double profile = radius * radius / variance * std::exp(-.5 * (dx * dx + dy * dy) / variance);
				result.opticalDepth += (.2 + Random(key + 3)) * profile;
				result.opticalPathUM += .04 * (Random(key + 4) - .5) * profile;
			}
		// Particles attenuate amplitude; their diffraction is produced by the
		// propagation operator, not a rainbow sprite placed around each particle.
		for (uint32_t i = 0; i < 32; ++i) {
			uint32_t key = seed + 13 * i;
			double dx = x - (Random(key) - .5) * 16, dy = y - (Random(key + 1) - .5) * 16;
			double radius = .05 + .10 * Random(key + 2);
			double variance = radius * radius + footprint;
			if (dx * dx + dy * dy < 20 * variance)
				result.opticalDepth += (1 + 3 * Random(key + 3)) * radius * radius / variance * std::exp(-.5 * (dx * dx + dy * dy) / variance);
		}
		// Shallow curved grooves modulate phase as well as amplitude. Integrating
		// their transverse profile over the pupil pixel avoids disappearing lines.
		for (uint32_t i = 0; i < 10; ++i) {
			uint32_t key = seed + 571 + 11 * i;
			double dx = x - (Random(key) - .5) * 12, dy = y - (Random(key + 1) - .5) * 12;
			double angle = 2 * pi * Random(key + 2), c = std::cos(angle), s = std::sin(angle);
			double u = c * dx + s * dy, v = -s * dx + c * dy;
			double length = .5 + 3 * Random(key + 3), width = .04 + .06 * Random(key + 4);
			v -= (Random(key + 5) - .5) * .3 * u * u;
			double variance = width * width + footprint;
			if (std::abs(u) < 1.5 * length && v * v < 20 * variance) {
				double profile = width / std::sqrt(variance) * std::exp(-.5 * v * v / variance - std::pow(u / length, 8));
				result.opticalDepth += .18 * profile;
				result.opticalPathUM += (.04 + .18 * Random(key + 6)) * profile;
			}
		}
		// A thin curved film ridge family supplies coherent curved streaks. Its
		// dimensions/depth are empirical, but wavelength-dependent phase and the
		// interference with the clean aperture are evaluated by wave propagation.
		double cx = (Random(seed + 701) - .5) * 6, cy = (Random(seed + 702) - .5) * 6;
		double dx = x - cx, dy = y - cy;
		double period = .10 + .08 * Random(seed + 703);
		double radius = std::hypot(dx + 6, dy);
		double envelope = std::exp(-.5 * (dx * dx / 5 + dy * dy / 3));
		double filter = std::exp(-.5 * std::pow(2 * pi / period, 2) * footprint);
		double modulation = .5 + .5 * std::sin(1.7 * dx + .6 * std::sin(dy * 1.3));
		double ridge = std::cos(2 * pi * radius / period + 1.2 * std::sin(dy * 2) + .7 * std::sin(dx * 3.1));
		result.opticalPathUM += .035 * envelope * modulation * filter * ridge;
		result.opticalDepth += .025 * envelope;
		return result;
	}
}
