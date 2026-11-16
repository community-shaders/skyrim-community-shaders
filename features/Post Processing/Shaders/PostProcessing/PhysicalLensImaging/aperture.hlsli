#ifndef PHYSICAL_LENS_CAMERA_APERTURE
#define PHYSICAL_LENS_CAMERA_APERTURE
// Camera roundness uses the same equal-area polygon/circle interpolation as
// DoF. Coordinates here are divided by the circumscribed radius. Nonnegative
// curvature retains the independent lens preset's curved mechanical blades.
float CameraIrisClearance(float2 p, float blades, float rotation, float curvature)
{
	const float pi = 3.14159265359;
	float sector = 2 * pi / blades;
	float angle = atan2(p.y, abs(p.x) + abs(p.y) > 1e-8 ? p.x : 1e-8) - rotation;
	float delta = angle - sector * (floor(angle / sector) + .5);
	float roundness = saturate(-curvature - 1);
	float circum = sqrt(2 * pi / (blades * sin(sector)));
	float radius = lerp(circum * cos(pi / blades) / cos(delta), 1, roundness) / lerp(circum, 1, roundness);
	return 1 - length(p) / radius;
}
#endif
