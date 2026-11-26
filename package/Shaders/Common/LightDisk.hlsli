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
		const float cotRadius = disk.y / max(disk.x, EPSILON_DIVISION);
		const float boundary = clamp(-cotRadius * cosine / sine, -1.0, 1.0);
		const float tangent = sine * sqrt(saturate(1.0 - boundary * boundary));
		const float z = tangent / max(cotRadius, EPSILON_DIVISION);
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
		return max(0.0, (cosine * acos(boundary) + z + remainder / max(sinSquared, EPSILON_LENGTH_SQ)) / Math::PI);
	}

	float MeanCosine(float cosine, float2 disk)
	{
		return disk.x > 0.0 ? cosine * (0.5 * (1.0 + disk.y)) : cosine;
	}

	float MeanAbsCosine(float cosine, float2 disk)
	{
		return MeanCosine(2.0 * DiffuseCosine(cosine, disk) - cosine, disk);
	}

	float CosineVariance(float cosine, float2 disk)
	{
		if (disk.x <= 0.0)
			return 0.0;
		const float axial = (1.0 - disk.y) * (1.0 - disk.y) / 12.0;
		const float transverse = disk.x * disk.x * (2.0 + disk.y) / (6.0 * (1.0 + disk.y));
		return lerp(transverse, axial, saturate(cosine * cosine));
	}

	// Positive part of a linear function averaged over the projection of a uniform disk.
	float PositivePart(float center, float halfWidth)
	{
		if (abs(center) >= halfWidth)
			return max(center, 0.0);
		const float x = center / halfWidth;
		return (center * acos(-x) + halfWidth * ((2.0 + x * x) / 3.0) * sqrt(saturate(1.0 - x * x))) * Math::INV_PI;
	}

	// Match the first two moments of the spherical cap for a clamped, biased cosine.
	float WrappedCosine(float cosine, float bias, float scale, float2 disk)
	{
		if (disk.x <= 0.0)
			return saturate((cosine + bias) * scale);
		if (bias == 0.0 && scale == 1.0)
			return DiffuseCosine(cosine, disk);
		const float center = (MeanCosine(cosine, disk) + bias) * scale;
		const float halfWidth = 2.0 * sqrt(CosineVariance(cosine, disk)) * scale;
		return saturate(PositivePart(center, halfWidth) - PositivePart(center - 1.0, halfWidth));
	}

	// Three symmetric samples approximate the projected cosine from its mean and variance.
	float3 CosineSamples(float cosine, float2 disk)
	{
		const float mean = MeanCosine(cosine, disk);
		const float offset = sqrt(2.0 * CosineVariance(cosine, disk));
		return clamp(mean + float3(-offset, 0.0, offset), -1.0, 1.0);
	}

	// Four equal-weight directions at the mean solid-angle radius of a uniform cap.
	void QuadratureAxes(float3 light, float2 disk, out float3 center, out float3 x, out float3 y)
	{
		const float mean = 0.5 * (1.0 + disk.y);
		const float spread = disk.x * sqrt((1.0 + mean) / (2.0 * (1.0 + disk.y)));
		center = light * mean;
		x = spread * normalize(cross(abs(light.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0), light));
		y = cross(light, x);
	}

	float3 ClosestDirection(float3 axis, float3 light, float2 disk)
	{
		if (disk.x <= 0.0)
			return light;
		const float cosine = dot(axis, light);
		const float3 tangent = axis - cosine * light;
		const float3 edge = normalize(disk.y * light + disk.x * tangent * rsqrt(max(dot(tangent, tangent), 1e-12)));
		return cosine >= disk.y ? axis : edge;
	}

	// Representative point on the emitter cone nearest the reflected view ray.
	float3 SpecularDirection(float3 normal, float3 view, float3 light, float2 disk)
	{
		return ClosestDirection(reflect(-view, normal), light, disk);
	}

	// Average a cosine-power lobe over a disk centered on its peak. A Blinn half-vector
	// rotates by about half the incident light angle, so pass a quarter of its exponent.
	float PowerLobeNormalization(float exponent, float2 disk)
	{
		const float capHeight = disk.x * disk.x / (1.0 + disk.y);
		const float denominator = (exponent + 1.0) * capHeight;
		if (denominator < 0.01)
			return 1.0 - 0.5 * exponent * capHeight;
		return (1.0 - pow(saturate(disk.y), exponent + 1.0)) / max(denominator, 0.01);
	}

	float PhongCosine(float3 axis, float3 light, float exponent, float2 disk)
	{
		const float cosine = saturate(dot(axis, ClosestDirection(axis, light, disk)));
		return pow(cosine, exponent) * PowerLobeNormalization(exponent, disk);
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
