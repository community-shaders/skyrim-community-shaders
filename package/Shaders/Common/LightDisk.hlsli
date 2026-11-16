#ifndef LIGHT_DISK_HLSLI
#define LIGHT_DISK_HLSLI

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace LightDisk
{
	// Sin/cos of the emitter's angular radius. Point lights keep a zero sine.
	float2 DirectionalSource()
	{
#if defined(PHYSICAL_SKY)
		if (SharedData::physSkyData.enabled) {
			const float radius = SharedData::physSkyData.diskAngularRadius.w;
			return float2(sin(radius), cos(radius));
		}
#endif
		return float2(0.0, 1.0);
	}

	// Lambert irradiance of a spherical cap, normalized by pi sin^2(radius).
	// Lagarde and de Rousiers, Moving Frostbite to Physically Based Rendering (2014).
	float DiffuseCosine(float cosine, float2 disk)
	{
		const float sinSquared = disk.x * disk.x;
		if (sinSquared <= 0.0 || cosine * cosine >= sinSquared)
			return saturate(cosine);
		const float sine = sqrt(saturate(1.0 - cosine * cosine));
		const float cotRadius = disk.y / disk.x;
		const float boundary = clamp(-cotRadius * cosine / sine, -1.0, 1.0);
		const float tangent = sine * sqrt(saturate(1.0 - boundary * boundary));
		const float z = tangent / cotRadius;
		const float zSquared = z * z;
		// Factor atan(z) - z before division by the disc area. Direct subtraction
		// loses the entire small-disc terminator in shader-model-5 arithmetic.
		float remainder;
		if (z < 0.5) {
			float polynomial = 1.0 / 21.0;
			polynomial = 1.0 / 19.0 - zSquared * polynomial;
			polynomial = 1.0 / 17.0 - zSquared * polynomial;
			polynomial = 1.0 / 15.0 - zSquared * polynomial;
			polynomial = 1.0 / 13.0 - zSquared * polynomial;
			polynomial = 1.0 / 11.0 - zSquared * polynomial;
			polynomial = 1.0 / 9.0 - zSquared * polynomial;
			polynomial = 1.0 / 7.0 - zSquared * polynomial;
			polynomial = 1.0 / 5.0 - zSquared * polynomial;
			polynomial = 1.0 / 3.0 - zSquared * polynomial;
			remainder = -z * zSquared * polynomial;
		} else {
			remainder = atan(z) - z;
		}
		return max(0.0, (cosine * acos(boundary) + z + remainder / sinSquared) / Math::PI);
	}

	// Representative point on the emitter cone nearest the reflected view ray.
	float3 SpecularDirection(float3 normal, float3 view, float3 light, float2 disk)
	{
		if (disk.x <= 0.0)
			return light;
		const float3 reflection = reflect(-view, normal);
		const float cosine = dot(reflection, light);
		const float3 tangent = reflection - cosine * light;
		const float3 edge = normalize(disk.y * light + disk.x * tangent * rsqrt(max(dot(tangent, tangent), 1e-12)));
		return cosine >= disk.y ? reflection : edge;
	}

	// Karis, Real Shading in Unreal Engine 4 (2013), finite-source GGX normalization.
	float GGXNormalization(float roughness, float2 disk)
	{
		if (disk.x <= 0.0)
			return 1.0;
		const float alpha = roughness * roughness;
		const float broadenedAlpha = min(1.0, alpha + 0.5 * disk.x);
		const float ratio = alpha / max(broadenedAlpha, 1e-8);
		return ratio * ratio;
	}
}

#endif
