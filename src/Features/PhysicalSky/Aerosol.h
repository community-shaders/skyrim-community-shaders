#pragma once

namespace Aerosol
{
	enum class Type : uint32_t
	{
		Custom,
		Continental,
		Maritime,
		Urban,
		Desert,
		Count
	};

	struct Optics
	{
		float3 scatter;
		float3 absorption;
		float phaseG;
	};

	Optics Evaluate(Type type, float loading, float relativeHumidity);
	Optics Lerp(const Optics& a, const Optics& b, float t);
}
