#include "PostProcessing/PhysicalLensImaging/aperture.hlsli"
// Direct-path optical table generation. Cost depends on table samples, not
// screen resolution. CPU double-precision reference: PhysicalLensModel.h.
struct Surface
{
	float4 Geometry;  // z, radius, clear semi-diameter, stop
	float4 Media;     // nd before, Cauchy B before, nd after, Cauchy B after
	float4 Asphere;
	float4 HighOrder;   // A12
	float4 Dispersion;  // C,D curvature before and after the interface.
	float4 CoatingTable[72];
};
cbuffer ImagingParameters : register(b0)
{
	uint SurfaceCount, RayCount;
	float SensorZ, Coating;
	uint Blades;
	float Rotation;
	float BladeCurvature, Padding;
	Surface Surfaces[40];
};
struct RayInput
{
	float4 PupilField, Conjugate;
};
struct RayOutput
{
	float4 SensorEnergy, Direction;
};
StructuredBuffer<RayInput> InputRays : register(t0);
RWStructuredBuffer<RayOutput> OutputRays : register(u0);
static const float PI = 3.14159265359;

#include "PostProcessing/PhysicalLensImaging/dispersion.hlsli"

bool Sag(Surface s, float h2, out float sag, out float gradient)
{
	float curvature = s.Geometry.y == 0 ? 0 : rcp(s.Geometry.y);
	float q = 1 - h2 * curvature * curvature;
	sag = gradient = 0;
	if (q <= 0)
		return false;
	float root = sqrt(q);
	float a12 = s.HighOrder.x;
	sag = h2 * curvature / (1 + root) + h2 * h2 * (s.Asphere.x + h2 * (s.Asphere.y + h2 * (s.Asphere.z + h2 * (s.Asphere.w + h2 * a12))));
	gradient = curvature / root + h2 * (4 * s.Asphere.x + h2 * (6 * s.Asphere.y + h2 * (8 * s.Asphere.z + h2 * (10 * s.Asphere.w + h2 * 12 * a12))));
	return true;
}

bool Intersect(Surface s, inout float3 origin, float3 direction, out float3 normal)
{
	normal = 0;
	if (direction.z <= 1e-6)
		return false;
	float t = (s.Geometry.x - origin.z) / direction.z;
	[loop] for (uint iteration = 0; iteration < 12; ++iteration)
	{
		float3 p = origin + t * direction;
		float sag, gradient;
		if (!Sag(s, dot(p.xy, p.xy), sag, gradient))
			return false;
		float residual = p.z - s.Geometry.x - sag;
		if (abs(residual) < 3e-5) {
			if (t < -1e-5 || !isfinite(t))
				return false;
			origin = p;
			normal = normalize(float3(gradient * p.xy, -1));
			return true;
		}
		float derivative = direction.z - gradient * dot(p.xy, direction.xy);
		if (abs(derivative) < 1e-6)
			return false;
		t -= residual / derivative;
	}
	return false;
}

float Reflectance(Surface s, float n1, float n2, float ci, float ct, float wavelength)
{
	float2 r = float2((n1 * ci - n2 * ct) / (n1 * ci + n2 * ct), (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct));
	float bare = .5 * dot(r, r);
	if (Coating <= 0 || min(n1, n2) > 1.001)
		return bare;
	float angle = saturate(n1 < n2 ? ci : ct) * 23;
	float wave = clamp((wavelength - .4) * (11 / .3), 0, 11);
	uint a = min(uint(angle), 22), w = min(uint(wave), 10);
	float2 row0 = float2(s.CoatingTable[a * 3 + w / 4][w % 4], s.CoatingTable[a * 3 + (w + 1) / 4][(w + 1) % 4]);
	float2 row1 = float2(s.CoatingTable[(a + 1) * 3 + w / 4][w % 4], s.CoatingTable[(a + 1) * 3 + (w + 1) / 4][(w + 1) % 4]);
	float2 rows = lerp(row0, row1, angle - a);
	return saturate(lerp(bare, lerp(rows.x, rows.y, wave - w), Coating));
}

// Accumulate only departure from the axial optical path. Subtracting two
// complete ~100 mm float paths would destroy the nanometre-scale wavefront.
void AddWaveSegment(float2 deltaXY, float axialLength, float sagDifference, float index, inout float2 path)
{
	precise float lateral = dot(deltaXY, deltaXY);
	precise float axial = axialLength + sagDifference;
	precise float excess = (lateral + sagDifference * (2 * axialLength + sagDifference)) /
	                       max(sqrt(lateral + axial * axial) + axialLength, 1e-12) * index;
	precise float value = excess - path.y;
	precise float sum = path.x + value;
	path.y = (sum - path.x) - value;
	path.x = sum;
}

RayOutput Trace(RayInput ray, bool wave = false, float apertureMargin = 0)
{
	RayOutput rejected = (RayOutput)0;
	if (wave)
		rejected.SensorEnergy.w = -1;
	float3 direction = normalize(float3(ray.PupilField.zw + ray.PupilField.xy * ray.Conjugate.x, 1));
	float3 origin = float3(ray.PupilField.xy, 0) - 100 * direction;
	float energy = 1;
	float clearance = 1;
	precise float2 wavePath = 0;
	float2 previousXY = ray.PupilField.xy;
	float previousSag = 0, previousVertex = 0;
	[loop] for (uint i = 0; i < SurfaceCount; ++i)
	{
		Surface s = Surfaces[i];
		float3 normal;
		if (!Intersect(s, origin, direction, normal))
			return rejected;
		if (wave) {
			float sag, gradient;
			Sag(s, dot(origin.xy, origin.xy), sag, gradient);
			AddWaveSegment(origin.xy - previousXY, s.Geometry.x - previousVertex, sag - previousSag,
				Index(s.Media.x, s.Media.y, ray.Conjugate.y, s.Dispersion.xy), wavePath);
			previousXY = origin.xy;
			previousSag = sag;
			previousVertex = s.Geometry.x;
		}
		float aperture = s.Geometry.z;
		if (s.Geometry.w != 0 && Blades >= 3) {
			float sector = 2 * PI / Blades;
			float angle = atan2(origin.y, origin.x) - Rotation;
			float delta = angle - sector * floor(angle / sector + .5);
			float2 local = length(origin.xy) * float2(cos(delta), sin(delta)) / aperture;
			float apothem = cos(PI / Blades), sag = (1 - apothem) * BladeCurvature;
			float edgeLength = 2 * sin(PI / Blades);
			float edge = apothem + sag - local.x - 4 * sag * local.y * local.y / (edgeLength * edgeLength);
			if (BladeCurvature < 0)
				edge = CameraIrisClearance(origin.xy / aperture, Blades, Rotation, BladeCurvature);
			if (wave)
				clearance = min(clearance, edge);
			else if (edge < 0)
				return rejected;
		}
		if (wave) {
			clearance = min(clearance, 1 - length(origin.xy) / aperture);
			if (clearance < -apertureMargin)
				return rejected;
		} else if (dot(origin.xy, origin.xy) > aperture * aperture)
			return rejected;
		if (s.Geometry.w != 0)
			continue;
		float n1 = Index(s.Media.x, s.Media.y, ray.Conjugate.y, s.Dispersion.xy);
		float n2 = Index(s.Media.z, s.Media.w, ray.Conjugate.y, s.Dispersion.zw);
		float ci = -dot(normal, direction), eta = n1 / n2;
		float k = 1 - eta * eta * (1 - ci * ci);
		if (k < 0 || ci < 0)
			return rejected;
		float ct = sqrt(k);
		energy *= 1 - Reflectance(s, n1, n2, ci, ct, ray.Conjugate.y);
		direction = normalize(eta * direction + (eta * ci - ct) * normal);
	}
	if (direction.z <= 0 || SensorZ <= origin.z)
		return rejected;
	RayOutput hit;
	hit.SensorEnergy = float4(origin.xy + direction.xy * ((SensorZ - origin.z) / direction.z), energy, wave ? clearance : 1);
	hit.Direction = float4(direction, 0);
	if (wave) {
		Surface last = Surfaces[SurfaceCount - 1];
		AddWaveSegment(hit.SensorEnergy.xy - previousXY, SensorZ - previousVertex, -previousSag,
			Index(last.Media.z, last.Media.w, ray.Conjugate.y, last.Dispersion.zw), wavePath);
		hit.Direction.w = wavePath.x;
	}
	return hit;
}

[numthreads(64, 1, 1)] void TraceSamples(uint3 id : SV_DispatchThreadID) {
	if (id.x < RayCount)
		OutputRays[id.x] = Trace(InputRays[id.x]);
}

cbuffer CameraTableParameters : register(b1)
{
	uint GridWidth, GridHeight, PupilSamples;
	float InverseFocusDistance;
	float GateWidth, GateHeight, ProjectionFocal, RayPlaneZ;
	float4 PupilTransfer[3];
};
RWTexture2DArray<float4> CameraMaps : register(u1);
groupshared float4 CameraSums[64];

// Pupil centroid and integrated throughput: distortion, color and vignetting.
// No scene depth or defocus blur is applied to the image.
[numthreads(64, 1, 1)] void BuildCameraMaps(uint3 id : SV_GroupID, uint lane : SV_GroupIndex) {
	uint band = id.z;
	float2 sensor = (float2(id.xy) / float2(GridWidth - 1, GridHeight - 1) - .5) * float2(GateWidth, GateHeight);
	float4 transfer = PupilTransfer[band];
	float2 scale = ProjectionFocal / float2(GateWidth, GateHeight);
	float4 sum = 0;
	[loop] for (uint sampleIndex = lane; sampleIndex < PupilSamples; sampleIndex += 64)
	{
		// Opposite pupil pairs cancel odd quadrature error on the optical axis.
		uint pairIndex = sampleIndex / 2;
		float angle = pairIndex * 2.399963229728653;
		float2 pupil = float2(cos(angle), sin(angle)) * sqrt((pairIndex + .5) / (PupilSamples / 2));
		pupil *= (sampleIndex & 1) ? -1 : 1;
		float2 slope = (pupil * (1.2 * transfer.z) - transfer.x * sensor) / transfer.y;
		RayInput ray;
		ray.PupilField = float4(sensor, slope);
		ray.Conjugate = float4(0, band == 0 ? .65 : band == 1 ? .55 :
																.45,
			0, 0);
		RayOutput hit = Trace(ray);
		float2 outgoing = hit.Direction.xy / max(hit.Direction.z, 1e-8);
		float2 uv = .5 - (outgoing + (hit.SensorEnergy.xy - RayPlaneZ * outgoing) * InverseFocusDistance) * scale;
		// dOmega*cos(theta); retain the finite-focus working-aperture loss.
		float weight = hit.SensorEnergy.z * 1.44 * transfer.w / ((1 + dot(slope, slope)) * (1 + dot(slope, slope)));
		sum += float4(uv * weight, weight, 0);
	}
	CameraSums[lane] = sum;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (lane < stride)
			CameraSums[lane] += CameraSums[lane + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0) {
		sum = CameraSums[0];
		float2 uv = float2(id.xy) / float2(GridWidth - 1, GridHeight - 1);
		float2 displacement = sum.xy / max(sum.z, 1e-8) - uv;
		// No overscan exists in a post-process. Bound the displacement smoothly
		// by the available border distance instead of zooming/cropping the scene,
		// stretching clamped texels, or returning black outside the input image.
		float2 margin = min(uv, 1 - uv);
		displacement *= margin * rsqrt(max(margin * margin + displacement * displacement, 1e-20));
		CameraMaps[id] = float4(uv + displacement, sum.z / PupilSamples, 0);
	}
}
