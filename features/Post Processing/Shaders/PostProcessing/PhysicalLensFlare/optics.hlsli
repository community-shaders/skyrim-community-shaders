#include "PostProcessing/PhysicalLensImaging/aperture.hlsli"
#ifndef PHYSICAL_LENS_OPTICS
#	define PHYSICAL_LENS_OPTICS

struct Interface
{
	float Z, Radius, Aperture, Stop;
	float NBefore, BBefore, NAfter, BAfter;
	float4 Asphere;
	float4 HighOrder;
	float4 Dispersion;  // C,D curvature before and after the interface.
	float4 CoatingTable[72];
};
struct GhostPath
{
	uint4 Indices;
	float4 SensorMap;
	float4 Spectrum[3];
	float4 Constraints[120];
};
cbuffer LensParameters : register(b0)
{
	uint Width, Height, SurfaceCount, PathCount;
	float SensorWidth, SensorHeight, SensorZ, FocalLength;
	float PupilRadius, Threshold, Intensity, Coating;
	uint Blades;
	float Rotation, OutputWidth, OutputHeight;
	Interface Surfaces[40];
	uint MaxSources;
	float ExposureDecode, ManualExposure, AutoExposureEnabled;
	float BladeCurvature;
	float GhostDiffraction, FNumber, BeamState;
	float CutoffRelease, AlignmentVariation, LensSeed, Reserved;
};
static const float PI = 3.14159265359;
StructuredBuffer<GhostPath> Paths : register(t2);
struct Source
{
	float4 Color;
	float4 Position;
};
struct Bundle
{
	float4 CenterFlux;
	float4 Covariance;
};
struct Job
{
	float4 Bounds;
	uint SourceIndex, PathIndex;
	float Weight, Padding;
};
#	ifndef PHYSICAL_LENS_PUPIL_GRID
#		define PHYSICAL_LENS_PUPIL_GRID 33
#	endif
static const uint Grid = PHYSICAL_LENS_PUPIL_GRID;
static const uint PathsPerSource = 24;
static const uint Vertices = Grid * Grid;

#	include "PostProcessing/PhysicalLensImaging/dispersion.hlsli"
// Fit the local dispersion tangent through the three already traced spectral
// centroids. End bands need this curvature term too; their secants noticeably
// overestimate the width of chromatic caustics. The RMS is in sensor units.
float2 GhostSpectralRMS(GhostPath path, float2 red, float2 green, float2 blue, uint band)
{
	float3 q = float3(path.Spectrum[0].x, path.Spectrum[1].x, path.Spectrum[2].x);
	float3 delta = float3(q.y - q.x, q.z - q.y, q.z - q.x);
	float3 inverse = delta / max(delta * delta, 1e-8);
	float2 first = (green - red) * inverse.x;
	float2 curvature = ((blue - green) * inverse.y - first) * inverse.z;
	return (first + curvature * (2 * q[band] - q.x - q.y)) * path.Spectrum[band].y;
}

// Select the vertex-facing spherical cap, not simply the nearest root of
// the complete sphere (which is wrong for concave and reverse-going rays).
bool Intersect(Interface s, inout float3 origin, float3 direction, out float3 normal, out float clearance, bool extend = false)
{
	normal = float3(0, 0, 1);
	clearance = -1e6;
	float t = -1;
	if (abs(s.Radius) < 1e-6) {
		if (abs(direction.z) < 1e-6)
			return false;
		t = (s.Z - origin.z) / direction.z;
	} else {
		float3 center = float3(0, 0, s.Z + s.Radius);
		float3 oc = origin - center;
		float b = dot(oc, direction);
		float dz = origin.z - s.Z;
		float c = dot(origin.xy, origin.xy) + dz * (dz - 2 * s.Radius);
		float discriminant = b * b - c;
		if (discriminant < 0)
			return false;
		float root = sqrt(discriminant);
		float q = -b - (b >= 0 ? root : -root);
		float2 roots = float2(q, c / (abs(q) > 1e-10 ? q : 1e-10));
		bool found = false;
		[unroll] for (uint i = 0; i < 2; ++i)
		{
			float3 hit = origin + roots[i] * direction;
			if ((extend || roots[i] > 1e-5) && (hit.z - center.z) * s.Radius <= 0 && (!found || abs(roots[i]) < abs(t))) {
				t = roots[i];
				found = true;
			}
		}
		if (!found)
			return false;
		normal = normalize(origin + t * direction - center);
	}
	if (any(s.Asphere != 0) || s.HighOrder.x != 0) {
		// Spherical base sag plus A4/A6/A8/A10. Analytic gradient for both
		// Newton intersection and Snell normal; no tessellated lens surfaces.
		[unroll] for (uint iteration = 0; iteration < 5; ++iteration)
		{
			float3 v = origin + t * direction;
			float h2 = dot(v.xy, v.xy), r = s.Radius;
			float root = sqrt(max(1 - h2 / (r * r), 1e-6));
			float sag = h2 / (r * (1 + root)) + h2 * h2 * (s.Asphere.x + h2 * (s.Asphere.y + h2 * (s.Asphere.z + h2 * (s.Asphere.w + h2 * s.HighOrder.x))));
			float gradient = 1 / (r * root) + h2 * (4 * s.Asphere.x + h2 * (6 * s.Asphere.y + h2 * (8 * s.Asphere.z + h2 * (10 * s.Asphere.w + 12 * h2 * s.HighOrder.x))));
			float derivative = direction.z - gradient * dot(v.xy, direction.xy);
			if (abs(derivative) < 1e-6)
				return false;
			t -= (v.z - s.Z - sag) / derivative;
		}
		float3 v = origin + t * direction;
		float h2 = dot(v.xy, v.xy), r = s.Radius;
		if (h2 >= r * r)
			return false;
		float root = sqrt(1 - h2 / (r * r));
		float sag = h2 / (r * (1 + root)) + h2 * h2 * (s.Asphere.x + h2 * (s.Asphere.y + h2 * (s.Asphere.z + h2 * (s.Asphere.w + h2 * s.HighOrder.x))));
		if (abs(v.z - s.Z - sag) > .002)
			return false;
		float gradient = 1 / (r * root) + h2 * (4 * s.Asphere.x + h2 * (6 * s.Asphere.y + h2 * (8 * s.Asphere.z + h2 * (10 * s.Asphere.w + 12 * h2 * s.HighOrder.x))));
		normal = normalize(float3(-gradient * v.xy, 1));
	}
	if ((!extend && t <= 1e-5) || !isfinite(t))
		return false;
	origin += t * direction;
	clearance = 1 - length(origin.xy) / s.Aperture;
	if (dot(normal, direction) > 0)
		normal = -normal;
// Planning encloses the circumscribed circular iris, independent of source
// azimuth and blade rotation. Actual ray shading retains the curved blades.
#	ifndef PHYSICAL_LENS_PLAN_BAKE
	if (s.Stop != 0) {
		float sector = 2 * PI / Blades;
		float angle = atan2(origin.y, abs(origin.x) + abs(origin.y) > 1e-8 ? origin.x : 1e-8) - Rotation;
		float delta = angle - sector * floor(angle / sector + .5);
		float2 local = length(origin.xy) * float2(cos(delta), sin(delta)) / s.Aperture;
		float apothem = cos(PI / Blades), sag = (1 - apothem) * BladeCurvature;
		float edgeLength = 2 * sin(PI / Blades);
		// Quadratic arcs through the polygon vertices, bulging toward the circular
		// wide-open pupil. This models the iris boundary, not polygonal lens glass.
		clearance = 1 - (local.x + 4 * sag * local.y * local.y / (edgeLength * edgeLength)) / (apothem + sag);
		if (BladeCurvature < 0)
			clearance = CameraIrisClearance(origin.xy / s.Aperture, Blades, Rotation, BladeCurvature);
	}
#	endif
	return true;
}

// Unpolarized Fresnel or a baked lossless multilayer interference response.
// The film is only applied to air/glass interfaces, never cemented glass.
float Reflectance(Interface s, float n1, float n2, float ci, float ct, float wavelength, bool uncoated = false)
{
	float2 r = float2((n1 * ci - n2 * ct) / (n1 * ci + n2 * ct), (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct));
	float bare = .5 * dot(r, r);
	if (uncoated || Coating <= 0 || min(n1, n2) > 1.001)
		return bare;
	float angle = saturate(n1 < n2 ? ci : ct) * 23;
	float wave = clamp((wavelength - .4) * (11 / .3), 0, 11);
	uint a = min(uint(angle), 22), w = min(uint(wave), 10);
	float2 row0 = float2(s.CoatingTable[a * 3 + w / 4][w % 4], s.CoatingTable[a * 3 + (w + 1) / 4][(w + 1) % 4]);
	float2 row1 = float2(s.CoatingTable[(a + 1) * 3 + w / 4][w % 4], s.CoatingTable[(a + 1) * 3 + (w + 1) / 4][(w + 1) % 4]);
	float2 rows = lerp(row0, row1, angle - a);
	return saturate(lerp(bare, lerp(rows.x, rows.y, wave - w), Coating));
}

// Exactly two designated reflections. Snell refraction on every other
// interface, including the reverse leg; the aperture is tested each crossing.
// Rotate only the preplanned entrance coordinates. The optical trace and
// aperture coordinates remain in the actual lens frame.
float2 PupilToLens(float2 pupil, float2 sourceUV)
{
#	ifdef PHYSICAL_LENS_PLANNING
	if (Reserved > 0) {
		float2 slope = (.5 - sourceUV) * float2(SensorWidth, SensorHeight);
		float r = length(slope);
		float2 axis = r > 1e-8 ? slope / r : float2(1, 0);
		return float2(axis.x * pupil.x - axis.y * pupil.y, axis.y * pupil.x + axis.x * pupil.y);
	}
#	endif
	return pupil;
}
float4 TraceDetailedStops(float2 pupil, float2 uv, uint2 path, float wavelength, out float4 aperture, out float4 secondStop, out float4 thirdStop, bool pupilProbe, bool extend)
{
	// Preserve the aperture's two-dimensional mapping independently from glass
	// clipping. Hullin 2011 section 4.1 defers both tests until rasterization.
	aperture = float4(0, 0, 1, 1e6);
	secondStop = thirdStop = float4(0, 0, 1, 0);
	uint stops = 0;
	float3 direction = normalize(float3((.5 - uv) * float2(SensorWidth, SensorHeight) / FocalLength, 1));
	float3 origin = float3(pupil, 0) - direction * 10;
	float energy = 1, clearance = 1;
	const float4 rejected = float4(0, 0, 0, -1e6);
	int step = 1, surface = 0;
	uint reflections = 0;
	[loop] for (uint iteration = 0; iteration < 120; ++iteration)
	{
		if (surface >= (int)SurfaceCount)
			break;
		if (surface < 0)
			return rejected;
		Interface s = Surfaces[surface];
		float3 normal;
		float surfaceClearance;
		if (!Intersect(s, origin, direction, normal, surfaceClearance, extend))
			return rejected;
		clearance = min(clearance, surfaceClearance);
		if (s.Stop != 0) {
			// Coordinates from different crossings are different affine maps.
			// Selecting the limiting crossing per vertex creates discontinuities
			// and false serrated rims when that selection changes inside a cell.
			if (stops == 0)
				aperture.xy = origin.xy / s.Aperture;
			else if (stops == 1)
				secondStop = float4(origin.xy / s.Aperture, surfaceClearance, 1);
			else
				thirdStop = float4(origin.xy / s.Aperture, surfaceClearance, 1);
			aperture.w = min(aperture.w, surfaceClearance);
			++stops;
		} else
			aperture.z = min(aperture.z, surfaceClearance);
		if (s.Stop == 0) {
			float n1 = Index(step > 0 ? s.NBefore : s.NAfter, step > 0 ? s.BBefore : s.BAfter, wavelength, step > 0 ? s.Dispersion.xy : s.Dispersion.zw);
			float n2 = Index(step > 0 ? s.NAfter : s.NBefore, step > 0 ? s.BAfter : s.BBefore, wavelength, step > 0 ? s.Dispersion.zw : s.Dispersion.xy);
			float ci = saturate(-dot(normal, direction));
			float eta = n1 / n2;
			float k = 1 - eta * eta * (1 - ci * ci);
			bool bounce = (reflections == 0 && surface == (int)path.x) || (reflections == 1 && surface == (int)path.y);
			if (k < 0 && !bounce)
				return rejected;
			float ct = sqrt(max(k, 0));
			float r = k < 0 ? 1 : Reflectance(s, n1, n2, ci, ct, wavelength, pupilProbe);
			if (bounce) {
				direction = reflect(direction, normal);
				energy *= r;
				step = -step;
				++reflections;
			} else {
				direction = normalize(eta * direction + (eta * ci - ct) * normal);
				energy *= 1 - r;
			}
		}
		surface += step;
	}
	if (reflections != 2 || surface != (int)SurfaceCount || direction.z <= 1e-5)
		return rejected;
	float t = (SensorZ - origin.z) / direction.z;
	if (!extend && t <= 0)
		return rejected;
	float2 sensor = origin.xy + t * direction.xy;
	// Paper section 5: small, fixed per-ghost displacement breaks perfect
	// coaxial symmetry. This is an artistic assembly-tolerance fit in mm,
	// not a claim to trace a measured decentered production lens.
	uint key = (path.x + 1) * 73856093u ^ (path.y + 1) * 19349663u ^ (uint(LensSeed) * 83492791u);
	key ^= key >> 16;
	key *= 0x7feb352du;
	key ^= key >> 15;
	float2 offset = float2(key & 65535u, key >> 16) / 65535. - .5;
	sensor += AlignmentVariation * .3 * offset;
	float2 position = -2 * sensor / float2(SensorWidth, SensorHeight);
	// Clip-space Y points up; source UV Y points down.
	position.y = -position.y;
	return float4(position, energy, clearance);
}
float4 TraceDetailed(float2 pupil, float2 uv, uint2 path, float wavelength, out float4 aperture, bool pupilProbe, bool extend)
{
	float4 secondStop, thirdStop;
	return TraceDetailedStops(pupil, uv, path, wavelength, aperture, secondStop, thirdStop, pupilProbe, extend);
}
float4 Trace(float2 pupil, float2 uv, uint2 path, float wavelength, bool pupilProbe = false)
{
	float4 aperture;
	return TraceDetailed(pupil, uv, path, wavelength, aperture, pupilProbe, false);
}
#endif
