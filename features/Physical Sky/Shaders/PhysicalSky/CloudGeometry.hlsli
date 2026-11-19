#ifndef PHYSICAL_SKY_CLOUD_GEOMETRY_HLSLI
#define PHYSICAL_SKY_CLOUD_GEOMETRY_HLSLI

float CloudCurvatureOffset(float2 relativeXY, float radius)
{
	return dot(relativeXY, relativeXY) / radius;
}

float CloudProfileAltitude(float3 position, float2 cameraXY, float radius)
{
	return position.z + CloudCurvatureOffset(position.xy - cameraXY, radius);
}

float2 IntersectCloudBelow(float3 origin, float3 direction, float altitude, float radius)
{
	const float a = dot(direction.xy, direction.xy) / radius;
	const float b = direction.z + 2.0 * dot(origin.xy, direction.xy) / radius;
	const float c = origin.z + CloudCurvatureOffset(origin.xy, radius) - altitude;
	if (a == 0.0) {
		if (b == 0.0)
			return c <= 0.0 ? float2(-1e30, 1e30) : float2(0, 0);
		const float root = -c / b;
		return b > 0.0 ? float2(-1e30, root) : float2(root, 1e30);
	}
	const float discriminant = b * b - 4.0 * a * c;
	if (discriminant <= 0.0)
		return 0.0;
	const float q = -0.5 * (b + (b < 0.0 ? -sqrt(discriminant) : sqrt(discriminant)));
	const float2 roots = float2(q / a, c / q);
	return float2(min(roots.x, roots.y), max(roots.x, roots.y));
}

#endif
