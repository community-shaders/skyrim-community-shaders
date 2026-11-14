#include "Aerosol.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Aerosol
{
	namespace
	{
		struct Sample
		{
			float3 scatter;
			float3 absorption;
			float scatterMoment;
		};

#include "AerosolData.inl"

		static_assert(std::size(samples) + 1 == static_cast<uint32_t>(Type::Count));

		float MeanCosine(float g)
		{
			const float g2 = g * g;
			return 3.f * g * (4.f + g2) / (5.f * (2.f + g2));
		}

		float PhaseG(float meanCosine)
		{
			float lower = -0.999f;
			float upper = 0.999f;
			for (int i = 0; i < 24; ++i) {
				const float g = (lower + upper) * 0.5f;
				if (MeanCosine(g) < meanCosine)
					lower = g;
				else
					upper = g;
			}
			return (lower + upper) * 0.5f;
		}
	}

	Optics Evaluate(Type type, float loading, float relativeHumidity)
	{
		if (type <= Type::Custom || type >= Type::Count)
			return {};

		loading = std::isfinite(loading) ? std::clamp(loading, 0.f, 10.f) : 1.f;
		relativeHumidity = std::isfinite(relativeHumidity) ? std::clamp(relativeHumidity, 0.f, 99.f) : 50.f;
		uint32_t upper = 1;
		while (upper < std::size(humidityLevels) - 1 && relativeHumidity > humidityLevels[upper])
			++upper;
		const auto& lowerSample = samples[static_cast<uint32_t>(type) - 1][upper - 1];
		const auto& upperSample = samples[static_cast<uint32_t>(type) - 1][upper];
		const float t = (relativeHumidity - humidityLevels[upper - 1]) / (humidityLevels[upper] - humidityLevels[upper - 1]);
		const float3 scatter = lowerSample.scatter * (1.f - t) + upperSample.scatter * t;
		const float3 absorption = lowerSample.absorption * (1.f - t) + upperSample.absorption * t;
		const float moment = std::lerp(lowerSample.scatterMoment, upperSample.scatterMoment, t);
		return { scatter * loading, absorption * loading, PhaseG(moment / scatter.y) };
	}

	Optics Lerp(const Optics& a, const Optics& b, float t)
	{
		t = std::isfinite(t) ? std::clamp(t, 0.f, 1.f) : 0.f;
		if (t == 0.f)
			return a;
		if (t == 1.f)
			return b;

		const float3 scatter = a.scatter * (1.f - t) + b.scatter * t;
		const float3 absorption = a.absorption * (1.f - t) + b.absorption * t;
		const float moment = std::lerp(a.scatter.y * MeanCosine(a.phaseG), b.scatter.y * MeanCosine(b.phaseG), t);
		const float phaseG = scatter.y > 0.f ? PhaseG(moment / scatter.y) : 0.f;
		return { scatter, absorption, phaseG };
	}
}
