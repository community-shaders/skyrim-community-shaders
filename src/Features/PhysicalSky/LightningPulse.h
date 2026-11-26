#pragma once

#include <algorithm>
#include <cmath>

namespace LightningPulse
{
	struct Parameters
	{
		unsigned count = 3;
		double duration = 0.020;
		double interval = 0.090;
		double decay = 0.65;
	};

	inline double Duration(const Parameters& parameters)
	{
		return (parameters.count - 1u) * parameters.interval + parameters.duration;
	}

	// Integrating each stroke over the display interval preserves its energy
	// when a 20 ms return stroke falls between two 30 Hz frames.
	// Artist-scalable optical envelope, not an electrical current model.
	// Reach the configured peak quickly, then decay smoothly to zero at duration.
	inline double StrokeIntegral(double time, double duration)
	{
		const double t = std::clamp(time, 0.0, duration);
		const double rise = duration * 0.05;
		if (t <= rise)
			return t * t / (2.0 * rise);
		const double fall = duration - rise;
		const double u = (t - rise) / fall;
		const double tail = std::exp(-6.0);
		return rise * 0.5 + fall * (-std::expm1(-6.0 * u) / 6.0 - tail * u) / (1.0 - tail);
	}

	inline float Average(double begin, double end, const Parameters& parameters)
	{
		if (end <= begin)
			return 0.f;
		double energy = 0.0;
		for (unsigned i = 0; i < parameters.count; ++i) {
			const double start = i * parameters.interval;
			if (start >= end)
				break;
			if (start + parameters.duration <= begin)
				continue;
			energy += std::pow(parameters.decay, i) * (StrokeIntegral(end - start, parameters.duration) - StrokeIntegral(begin - start, parameters.duration));
		}
		return static_cast<float>(energy / (end - begin));
	}
}
