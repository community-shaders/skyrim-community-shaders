#include "PostProcessing/PhysicalLensFlare/flare.cs.hlsl"
#ifdef PHYSICAL_LENS_AREA_LAYERS
#	include "PostProcessing/PhysicalLensFlare/area.hlsli"
#endif

// Six ray planes: moments, first aperture/glass clipping, raw power/pupil,
// Gouraud density/spectral slope, second aperture, third aperture.
static const uint RayPlane = PathsPerSource * 3 * Vertices;
[numthreads(64, 1, 1)] void TraceBeams(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	Job job = Jobs[group.y];
	uint grid = (uint)job.Padding, i = group.x * 64 + lane;
	if (i >= grid * grid)
		return;
	uint2 xy = uint2(i % grid, i / grid);
	float2 coordinate = float2(PupilCoordinates[group.y * Grid + xy.x].x, PupilCoordinates[group.y * Grid + xy.y].y);
	float2 lo = float2(PupilCoordinates[group.y * Grid + max(int(xy.x) - 1, 0)].x, PupilCoordinates[group.y * Grid + max(int(xy.y) - 1, 0)].y);
	float2 hi = float2(PupilCoordinates[group.y * Grid + min(xy.x + 1, grid - 1)].x, PupilCoordinates[group.y * Grid + min(xy.y + 1, grid - 1)].y);
	float2 width = .5 * (hi - lo) * (job.Bounds.zw - job.Bounds.xy);
	float4 aperture, secondStop, thirdStop;
	GhostPath path = Paths[job.PathIndex];
	float4 ray = TraceDetailedStops(PupilToLens(lerp(job.Bounds.xy, job.Bounds.zw, coordinate), Sources[job.SourceIndex].Position.xy), Sources[job.SourceIndex].Position.xy,
		path.Indices.xy, rsqrt(path.Spectrum[group.z].x), aperture, secondStop, thirdStop, false, true);
	ray.z *= path.Spectrum[group.z].z;
	uint index = (group.y * 3 + group.z) * Vertices + i;
	OutRays[index + RayPlane] = aperture;
	OutRays[index + 4 * RayPlane] = secondStop;
	OutRays[index + 5 * RayPlane] = thirdStop;
	OutRays[index + 2 * RayPlane] = float4(ray.z, width.x * width.y, lerp(job.Bounds.xy, job.Bounds.zw, coordinate));
	float coverage = saturate(ray.w / max(2 * max(hi.x - lo.x, hi.y - lo.y), 1e-6) + .5);
	ray.z *= coverage * width.x * width.y / (PI * PupilRadius * PupilRadius);
	OutRays[index] = ray;
}

	// Gouraud irradiance (Hullin section 4.3). Compute after tracing so each node
	// sees complete neighboring beams, including geometrically blocked vertices.
	[numthreads(64, 1, 1)] void ShadeBeams(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	Job job = Jobs[group.y];
	uint grid = (uint)job.Padding, i = group.x * 64 + lane;
	if (i >= grid * grid)
		return;
	int2 node = int2(i % grid, i / grid);
	uint base = (group.y * 3 + group.z) * Vertices;
	float sum = 0, weight = 0;
	float minimumMinor = 1e20;
	[unroll] for (int y = -1; y <= 0; ++y)
		[unroll] for (int x = -1; x <= 0; ++x)
	{
		int2 cell = node + int2(x, y);
		if (any(cell < 0) || any(cell + 1 >= int(grid)))
			continue;
		uint a = base + cell.y * grid + cell.x;
		float4 h0 = OutRays[a], h1 = OutRays[a + 1], h2 = OutRays[a + grid], h3 = OutRays[a + grid + 1];
		if (min(min(h0.w, h1.w), min(h2.w, h3.w)) < -1e5)
			continue;
		float2 d1 = h1.xy - h0.xy, d2 = h2.xy - h0.xy, d3 = h1.xy - h3.xy, d4 = h2.xy - h3.xy;
		float area0 = abs(d1.x * d2.y - d1.y * d2.x), area1 = abs(d3.x * d4.y - d3.y * d4.x);
		float area = .5 * (area0 + area1);
		float2 dx = .25 * (h1.xy - h0.xy + h3.xy - h2.xy) * float2(OutputWidth, OutputHeight);
		float2 dy = .25 * (h2.xy - h0.xy + h3.xy - h1.xy) * float2(OutputWidth, OutputHeight);
		float trace = dot(dx, dx) + dot(dy, dy), determinant = dx.x * dy.y - dx.y * dy.x;
		float minor = determinant * determinant / max(6 * (trace + sqrt(max(trace * trace - 4 * determinant * determinant, 0))), 1e-20);
		minimumMinor = min(minimumMinor, minor);
		float4 p0 = OutRays[a + 2 * RayPlane], p1 = OutRays[a + 1 + 2 * RayPlane];
		float4 p2 = OutRays[a + grid + 2 * RayPlane], p3 = OutRays[a + grid + 1 + 2 * RayPlane];
		float pupilArea = abs((p3.z - p0.z) * (p3.w - p0.w));
		// Lump the incident triangles' area at their shared nodes. This preserves
		// integrated, unclipped beam power AND continuity: per-cell rescaling in
		// the vertex shader would give one shared edge two different irradiances.
		uint corner = uint(-y) * 2 + uint(-x);
		float incidentArea = (corner != 3 ? area0 : 0) + (corner != 0 ? area1 : 0);
		// Preserve the geometric Jacobian even below a pixel. A pixel-area
		// floor loses flux when a small cell still uses continuous reconstruction.
		// The finite-source/pixel kernels regularize its footprint later; truly
		// collapsed cells use their integrated power instead of this density.
		sum += incidentArea * (p0.x + p1.x + p2.x + p3.x) * pupilArea / (PI * PupilRadius * PupilRadius * max(area, 1e-20));
		weight += incidentArea;
	}
	// A separate plane avoids an SRV/UAV race on the raw power records.
	float4 r = OutRays[group.y * 3 * Vertices + i], g = OutRays[(group.y * 3 + 1) * Vertices + i], b = OutRays[(group.y * 3 + 2) * Vertices + i];
	GhostPath path = Paths[job.PathIndex];
	float2 spectral = min(min(r.w, g.w), b.w) > -1e5 ? GhostSpectralRMS(path, r.xy, g.xy, b.xy, group.z) * .5 * float2(OutputWidth, OutputHeight) : 0;
	Source source = Sources[job.SourceIndex];
	float magnification = path.SensorMap.y / FocalLength;
	float blur = magnification * magnification * dot(source.Position.zw, float2(OutputWidth * OutputWidth, OutputHeight * OutputHeight)) + dot(spectral, spectral);
	float filtered = max(1 - smoothstep(.06, .2, minimumMinor), smoothstep(.5, 1.5, blur / max(.5 * minimumMinor, .25)));
#ifdef PHYSICAL_LENS_AREA_LAYERS
	// Do not reintroduce a sharp unfiltered copy of a broad emitter merely
	// because its optical cell is large. Small sources retain the original mix.
	float sourceBlur = magnification * magnification * dot(source.Position.zw, float2(OutputWidth * OutputWidth, OutputHeight * OutputHeight));
	filtered = max(filtered, smoothstep(8, 16, SourceMinorSigma(source)) * smoothstep(1, 4, sourceBlur));
#endif
	// Filtered/unfiltered power is a shared nodal field. Cell-local switches
	// create a lattice even when both reconstruction kernels are continuous.
	OutRays[base + i + 3 * RayPlane] = float4(sum / max(weight, 1e-20), spectral, filtered);
}
