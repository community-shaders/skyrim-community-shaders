#include "PostProcessing/PhysicalLensFlare/optics.hlsli"
#ifdef PHYSICAL_LENS_AREA_LAYERS
#	include "PostProcessing/PhysicalLensFlare/area.hlsli"
#endif

// Integrated RGB radiance, covariance XY; centroid UV, variance XX/YY.
// Tile-local moments and a weighted central-moment merge avoid catastrophic
// cancellation for tiny emitters near the edge of a 4K frame.
#include "PostProcessing/PhysicalLensFlare/exposure.hlsli"
Texture2D<float4> Scene : register(t0);
StructuredBuffer<Source> Sources : register(t1);
RWStructuredBuffer<Source> Candidates : register(u0);
RWStructuredBuffer<Source> Selected : register(u1);
// Before selection: lit count, regional mean, two component roots.
// After selection: threshold, regional mean, selected base/excess source + 1.
RWStructuredBuffer<float4> RegionData : register(u4);
RWStructuredBuffer<float4> OutRays : register(u1);
RWStructuredBuffer<Bundle> OutBundles : register(u4);
RWStructuredBuffer<Job> OutJobs : register(u2);
RWByteAddressBuffer Arguments : register(u3);
StructuredBuffer<Job> Jobs : register(t3);
#ifdef PHYSICAL_LENS_COMPACT_BEAMS
RWStructuredBuffer<uint> OutRasterQuads : register(u5);
RWByteAddressBuffer RasterArguments : register(u3);
groupshared uint RasterFirst, RasterCount, RasterQuadStart;
#endif
groupshared float PathScores[1024];
groupshared uint PathOrder[1024];
groupshared uint JobBase;
groupshared float4 PathBounds[768];
groupshared uint PickCount;
groupshared float LastScore;
groupshared float PilotWeights[49];
groupshared float4 PilotGeometry[49];
groupshared float4 BeamStatistics[64];
groupshared float2 PupilPDF[8];
StructuredBuffer<float2> PupilCoordinates : register(t4);
RWStructuredBuffer<float2> OutPupilCoordinates : register(u4);
StructuredBuffer<float4> TracedRays : register(t0);
float2 PupilQuantile(float2 q);
groupshared float3 Colors[64];
groupshared float3 Moments[64];
groupshared float3 Squares[64];
groupshared float Scores[64];
groupshared uint Winners[64];
groupshared float LocalThreshold;
groupshared float RegionLuminance;
groupshared uint EmptyTile;
groupshared float3 BrightColors[64], BrightMoments[64], BrightSquares[64];

float Luminance(float3 c) { return dot(c, float3(.2126, .7152, .0722)); }

// Small tiles preserve separated lights and provide enough parallel groups
// for a 4K input. Every pixel contributes, including subpixel highlights.
void ExtractTile(uint3 group, uint3 thread, uint lane, bool split)
{
	uint2 lo = group.xy * uint2(Width, Height) / uint2(64, 36);
	uint2 hi = (group.xy + 1) * uint2(Width, Height) / uint2(64, 36);
	if (lane == 0) {
		LocalThreshold = Threshold / max(VisibilityExposure(), 1e-20);
		RegionLuminance = split ? RegionData[group.y * 64 + group.x].y : 0;
		EmptyTile = split && RegionData[group.y * 64 + group.x].x == 0;
	}
	GroupMemoryBarrierWithGroupSync();
	// The first pass proves these tiles contain no above-threshold energy.
	// Skip their second full-resolution traversal without dropping tiny lights.
	if (EmptyTile != 0) {
		if (lane == 0) {
			Candidates[group.y * 64 + group.x] = (Source)0;
			Candidates[64 * 36 + group.y * 64 + group.x] = (Source)0;
		}
		return;
	}

	float3 color = 0, moment = 0, square = 0;
	float3 brightColor = 0, brightMoment = 0, brightSquare = 0;
	float count = 0;
	for (uint y = lo.y + thread.y; y < hi.y; y += 8)
		for (uint x = lo.x + thread.x; x < hi.x; x += 8) {
			float3 c = max(Scene[uint2(x, y)].rgb, 0);
			if (!all(isfinite(c)))
				continue;
			float luma = Luminance(c);
			if (luma <= LocalThreshold)
				continue;
			count += 1;
			// Fade the contrast classification over 1--2 stops, but subtract only
			// the region pedestal. Subtracting the classification threshold would
			// erode partially covered solar-edge pixels and shift their centroid.
			float pedestal = max(RegionLuminance, LocalThreshold);
			float3 excess = c * saturate((luma - pedestal) / max(luma, 1e-20)) * smoothstep(2 * pedestal, 4 * pedestal, luma);
			c *= saturate((luma - LocalThreshold) / max(luma, 1e-20));
			if (split)
				c = max(c - excess, 0);
			c /= float(Width) * Height;
			float weight = Luminance(c);
			float2 uv = (float2(x, y) + .5 - lo) / float2(Width, Height);
			if (split) {
				excess /= float(Width) * Height;
				float brightWeight = Luminance(excess);
				brightColor += excess;
				brightMoment += float3(uv, 1) * brightWeight;
				brightSquare += float3(uv * uv, uv.x * uv.y) * brightWeight;
			}
			color += c;
			if (split) {
				moment += float3(uv, 1) * weight;
				square += float3(uv * uv, uv.x * uv.y) * weight;
			}
		}
	if (split) {
		BrightColors[lane] = brightColor;
		BrightMoments[lane] = brightMoment;
		BrightSquares[lane] = brightSquare;
	}
	Scores[lane] = count;
	Colors[lane] = color;
	Moments[lane] = moment;
	Squares[lane] = square;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (lane < stride) {
			if (split) {
				BrightColors[lane] += BrightColors[lane + stride];
				BrightMoments[lane] += BrightMoments[lane + stride];
				BrightSquares[lane] += BrightSquares[lane + stride];
			}
			Scores[lane] += Scores[lane + stride];
			Colors[lane] += Colors[lane + stride];
			Moments[lane] += Moments[lane + stride];
			Squares[lane] += Squares[lane + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0) {
		uint tile = group.y * 64 + group.x;
		[unroll] for (uint band = 0; band < (split ? 2u : 1u); ++band)
		{
			float3 sumColor = band == 0 ? Colors[0] : BrightColors[0];
			float3 sumMoment = band == 0 ? Moments[0] : BrightMoments[0];
			float3 sumSquare = band == 0 ? Squares[0] : BrightSquares[0];
			float invWeight = rcp(max(sumMoment.z, 1e-20));
			Source s;
			float2 mean = sumMoment.xy * invWeight;
			float2 variance = max(sumSquare.xy * invWeight - mean * mean, 0);
			variance += rcp(12 * float2(Width * Width, Height * Height));
			s.Color = float4(sumColor, sumSquare.z * invWeight - mean.x * mean.y);
			s.Position = float4(mean + float2(lo) / float2(Width, Height), variance);
			Candidates[band * 64 * 36 + tile] = s;
		}
		if (!split)
			RegionData[tile] = float4(Scores[0], 0, 0, 0);
	}
}

[numthreads(8, 8, 1)] void Extract(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint lane : SV_GroupIndex) {
	ExtractTile(group, thread, lane, false);
}
	// Disjoint energy layers: a compact excess above the connected region's
	// radiance pedestal, plus the remaining broad illumination. Both preserve hue.
	[numthreads(8, 8, 1)] void ExtractLayers(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
	ExtractTile(group, thread, lane, true);
}

// Merge entire connected highlight regions before imposing the source budget.
// Tile ownership is spatial, not tied to the brightest texel, so a broad light
// does not break into competing 3x3 patches as it moves across the screen.
groupshared uint Labels[64 * 36];
groupshared uint Heads[64 * 36];
groupshared uint Next[64 * 36];
// At most 35 disjoint regions can contain more than 64 of the 2304 tiles.
groupshared uint LargeRoots[36];
groupshared uint LargeCount;
groupshared uint SelectedRoots[16];
Source MergeMoments(float3 color, float3 moment, float3 square, float2 origin)
{
	float2 mean = moment.xy / max(moment.z, 1e-20);
	float3 covariance = square / max(moment.z, 1e-20) - float3(mean * mean, mean.x * mean.y);
	covariance.xy = max(covariance.xy, 0);
	float limit = sqrt(covariance.x * covariance.y) * .999;
	Source result;
	result.Position = float4(origin + mean, covariance.xy);
	result.Color = float4(color, clamp(covariance.z, -limit, limit));
	return result;
}
uint Root(uint i)
{
	[allow_uav_condition] while (Labels[i] != i)
		i = Labels[i];
	return i;
}
void Join(uint a, uint b)
{
	[allow_uav_condition] while (true)
	{
		a = Root(a);
		b = Root(b);
		if (a == b)
			return;
		uint previous;
		InterlockedMin(Labels[max(a, b)], min(a, b), previous);
		if (previous == max(a, b))
			return;
	}
}
void MergeRegions(uint lane, uint offset, bool prepare)
{
	if (lane == 0)
		LargeCount = 0;
	uint i;
	for (i = lane; i < 64 * 36; i += 64) {
		Labels[i] = Luminance(Candidates[offset + i].Color.rgb) > 0 ? i : 0xffffffff;
		Heads[i] = 0xffffffff;
	}
	GroupMemoryBarrierWithGroupSync();
	for (i = lane; i < 64 * 36; i += 64) {
		if (Labels[i] == 0xffffffff)
			continue;
		if (i % 64 > 0 && Labels[i - 1] != 0xffffffff)
			Join(i, i - 1);
		if (i >= 64 && Labels[i - 64] != 0xffffffff)
			Join(i, i - 64);
	}
	GroupMemoryBarrierWithGroupSync();
	for (i = lane; i < 64 * 36; i += 64) {
		if (Labels[i] != 0xffffffff) {
			uint root = Root(i);
			Labels[i] = root;
			InterlockedExchange(Heads[root], i, Next[i]);
		}
	}
	GroupMemoryBarrierWithGroupSync();
	for (i = lane; i < 64 * 36; i += 64) {
		if (!prepare)
			RegionData[i][offset == 0 ? 2 : 3] = Labels[i] == 0xffffffff ? 0 : Labels[i] + offset + 1;
		if (Heads[i] == 0xffffffff)
			continue;
		uint cursor = Heads[i];
		for (uint links = 0; links < 64 && cursor != 0xffffffff; ++links)
			cursor = Next[cursor];
		if (cursor != 0xffffffff) {
			uint slot;
			InterlockedAdd(LargeCount, 1, slot);
			LargeRoots[slot] = i;
			continue;
		}
		Source result = (Source)0;
		float2 origin = Candidates[offset + i].Position.xy;
		float3 moment = 0, square = 0;
		float count = 0;
		for (uint j = Heads[i]; j != 0xffffffff; j = Next[j]) {
			Source s = Candidates[offset + j];
			if (prepare)
				count += RegionData[j].x;
			float w = Luminance(s.Color.rgb);
			float2 delta = s.Position.xy - origin;
			result.Color.rgb += s.Color.rgb;
			moment += float3(delta, 1) * w;
			square += w * (float3(s.Position.zw, s.Color.w) + float3(delta * delta, delta.x * delta.y));
			Candidates[offset + j] = (Source)0;
		}
		Candidates[offset + i] = MergeMoments(result.Color.rgb, moment, square, origin);
		if (prepare)
			RegionData[i].y = Threshold / max(VisibilityExposure(), 1e-20) + Luminance(result.Color.rgb) * float(Width) * Height / max(count, 1);
	}
	AllMemoryBarrierWithGroupSync();
	// Large panels/sky regions use all lanes; never serially visit thousands
	// of UAV records in the lane owning the component root.
	for (uint region = 0; region < LargeCount; ++region) {
		uint root = LargeRoots[region];
		float2 origin = Candidates[offset + root].Position.xy;
		float3 color = 0, moment = 0, square = 0;
		float count = 0;
		GroupMemoryBarrierWithGroupSync();
		for (i = lane; i < 64 * 36; i += 64) {
			if (Labels[i] != root)
				continue;
			Source s = Candidates[offset + i];
			if (prepare)
				count += RegionData[i].x;
			float w = Luminance(s.Color.rgb);
			float2 delta = s.Position.xy - origin;
			color += s.Color.rgb;
			moment += float3(delta, 1) * w;
			square += w * (float3(s.Position.zw, s.Color.w) + float3(delta * delta, delta.x * delta.y));
			Candidates[offset + i] = (Source)0;
		}
		Scores[lane] = count;
		Colors[lane] = color;
		Moments[lane] = moment;
		Squares[lane] = square;
		GroupMemoryBarrierWithGroupSync();
		[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
		{
			if (lane < stride) {
				Scores[lane] += Scores[lane + stride];
				Colors[lane] += Colors[lane + stride];
				Moments[lane] += Moments[lane + stride];
				Squares[lane] += Squares[lane + stride];
			}
			GroupMemoryBarrierWithGroupSync();
		}
		DeviceMemoryBarrierWithGroupSync();
		if (lane == 0) {
			Candidates[offset + root] = MergeMoments(Colors[0], Moments[0], Squares[0], origin);
			if (prepare)
				RegionData[root].y = Threshold / max(VisibilityExposure(), 1e-20) + Luminance(Colors[0]) * float(Width) * Height / max(Scores[0], 1);
		}
		DeviceMemoryBarrierWithGroupSync();
	}
}
[numthreads(64, 1, 1)] void PrepareRegions(uint lane : SV_GroupIndex) {
	MergeRegions(lane, 0, true);
	AllMemoryBarrierWithGroupSync();
	for (uint i = lane; i < 64 * 36; i += 64) {
		// Use candidate storage as a staging area so broadcasting thresholds
		// cannot race writes to a region root's metadata.
		Source s = (Source)0;
		s.Color.x = Labels[i] == 0xffffffff ? 0 : RegionData[Labels[i]].y;
		Candidates[i] = s;
	}
	DeviceMemoryBarrierWithGroupSync();
	for (uint tile = lane; tile < 64 * 36; tile += 64)
		RegionData[tile].y = Candidates[tile].Color.x;
}
	[numthreads(64, 1, 1)] void SelectSources(uint lane : SV_GroupIndex)
{
	MergeRegions(lane, 0, false);
	AllMemoryBarrierWithGroupSync();
	MergeRegions(lane, 64 * 36, false);
	AllMemoryBarrierWithGroupSync();
	[loop] for (uint source = 0; source < MaxSources; ++source)
	{
		float best = 0;
		uint winner = 0;
		for (uint i = lane; i < 2 * 64 * 36; i += 64) {
			float score = Luminance(Candidates[i].Color.rgb);
			if (score > best) {
				best = score;
				winner = i;
			}
		}
		Scores[lane] = best;
		Winners[lane] = winner;
		GroupMemoryBarrierWithGroupSync();
		[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
		{
			if (lane < stride && (Scores[lane + stride] > Scores[lane] ||
									 (Scores[lane + stride] == Scores[lane] && Winners[lane + stride] < Winners[lane]))) {
				Scores[lane] = Scores[lane + stride];
				Winners[lane] = Winners[lane + stride];
			}
			GroupMemoryBarrierWithGroupSync();
		}
		if (lane == 0) {
			SelectedRoots[source] = Scores[0] > 0 ? Winners[0] + 1 : 0;
			Source selected = Candidates[Winners[0]];
			// A subpixel highlight represented on a pixel lattice alternates
			// between one and two samples as it moves. Its measured covariance
			// then oscillates from 1/12 to 1/3 pixel^2, which a magnifying ghost
			// turns into visible breathing. Resolve that unknown subpixel extent
			// with a fixed pixel-response floor; resolved emitter areas are intact.
			if (BeamState > 0)
				selected.Position.zw = max(selected.Position.zw, rcp(3 * float2(Width * Width, Height * Height)));
			Selected[source] = selected;
			Candidates[Winners[0]] = (Source)0;
		}
		DeviceMemoryBarrierWithGroupSync();
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint tile = lane; tile < 64 * 36; tile += 64) {
		float4 region = RegionData[tile];
		uint2 roots = uint2(region.zw);
		region.x = Threshold / max(VisibilityExposure(), 1e-20);
		region.zw = 0;
		for (uint source = 0; source < MaxSources; ++source) {
			uint root = SelectedRoots[source];
			if (root != 0) {
				if (roots.x == root)
					region.z = source + 1;
				if (roots.y == root)
					region.w = source + 1;
			}
		}
		RegionData[tile] = region;
	}
}

float4 FitPupil(uint pathIndex, float2 uv)
{
	float radius = Surfaces[0].Aperture;
	float2 lo = -radius, hi = radius;
	float2 slope = (.5 - uv) * float2(SensorWidth, SensorHeight) / FocalLength;
	[loop] for (uint i = 0; i < Paths[pathIndex].Indices.z; ++i)
	{
		float2 constraint = Paths[pathIndex].Constraints[i].xy;
		float2 center = constraint.x * slope;
		// Conservative margin for spherical aberration and off-axis pupil shift;
		// exact tracing below performs all actual aperture/edge tests.
		float extent = constraint.y;
		lo = max(lo, center - extent);
		hi = min(hi, center + extent);
	}
	return float4(lo, max(lo, hi));
}

// Paraxial bounds are only a starting estimate. Strong ghost aberration can
// leave transmitting rays on their rectangular boundary, projecting the box
// into a four-cusped shape even for an on-axis source and circular lens glass.
groupshared uint Transmitting, ExpansionDone;
groupshared float4 ExpandedBounds;
// One group per selected path; evaluate all eight boundary probes in parallel.
[numthreads(64, 1, 1)] void ExpandJobs(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	Job job = OutJobs[group.y];
	Source source = Sources[job.SourceIndex];
	float radius = Surfaces[0].Aperture;
	float2 center = (job.Bounds.xy + job.Bounds.zw) * .5;
	float2 halfSize = (job.Bounds.zw - job.Bounds.xy) * .5;
	float2 lower = 0, upper = 0;
	uint refinements = 0;
	if (lane == 0) {
		ExpandedBounds = job.Bounds;
		ExpansionDone = 0;
	}
	[loop] for (uint iteration = 0; iteration < 8; ++iteration)
	{
		if (lane == 0)
			Transmitting = 0;
		GroupMemoryBarrierWithGroupSync();
		if (lane < 8) {
			float2 border[8] = { float2(-1, -1), float2(0, -1), float2(1, -1), float2(1, 0), float2(1, 1), float2(0, 1), float2(-1, 1), float2(-1, 0) };
			float2 pupil = lerp(ExpandedBounds.xy, ExpandedBounds.zw, border[lane] * .5 + .5);
			float4 ray = Trace(pupil, source.Position.xy, Paths[job.PathIndex].Indices.xy, .550, true);
			if (ray.w > -.05)
				InterlockedOr(Transmitting, 1);
		}
		GroupMemoryBarrierWithGroupSync();
		if (lane == 0) {
			if (Transmitting == 0) {
				upper = halfSize;
				if (all(lower == 0))
					ExpansionDone = 1;
			} else {
				lower = halfSize;
			}
			// Refine the enclosing bracket instead of switching the sample domain
			// abruptly by 1.75x when a border ray crosses the iris. Keep the same
			// eight-iteration cap and always retain a tested enclosing upper bound.
			if (any(upper > 0)) {
				if (++refinements >= 4 || iteration == 7)
					ExpansionDone = 1;
				halfSize = ExpansionDone != 0 ? upper : (lower + upper) * .5;
			} else
				halfSize *= 1.75;
			ExpandedBounds = float4(max(center - halfSize, -radius), min(center + halfSize, radius));
			if (all(ExpandedBounds.xy <= -radius) && all(ExpandedBounds.zw >= radius))
				ExpansionDone = 1;
		}
		GroupMemoryBarrierWithGroupSync();
		if (ExpansionDone != 0)
			break;
	}
	job.Bounds = ExpandedBounds;
	float2 footprint = abs(Paths[job.PathIndex].SensorMap.x) * (job.Bounds.zw - job.Bounds.xy) / float2(SensorWidth, SensorHeight) * float2(OutputWidth, OutputHeight);
	uint grid = PupilRadius > 9 || max(footprint.x, footprint.y) > 96 ? 17 : 15;
	if (Grid > 33)
		grid = Grid;
	// Importance probes use uncoated throughput: a green AR cancellation must
	// not undersample a pupil that still transmits red/blue ghosts. Adjacent
	// bands share the same geometry and sample positions.
	if (lane < 49) {
		float2 uv = (float2(lane % 7, lane / 7) + .5) / 7;
		float4 pilot = Trace(lerp(job.Bounds.xy, job.Bounds.zw, uv), source.Position.xy, Paths[job.PathIndex].Indices.xy, .550, true);
		PilotGeometry[lane] = pilot;
		float edge = saturate(1 - abs(pilot.w) * 3);
		PilotWeights[lane] = (edge * edge + .25 * saturate(pilot.w * 3 + .5)) * sqrt(max(pilot.z, 0));
	}
	GroupMemoryBarrierWithGroupSync();
	// Hullin section 4.2: nonuniform beam areas reveal optical distortion.
	// Screen-space curvature adds an error scale, so a large affine ghost does
	// not consume the same ray budget as a folded or strongly curved one.
	if (BeamState > 0) {
		float4 stats = 0;
		if (lane < 36) {
			uint i = (lane / 6) * 7 + lane % 6;
			float4 a = PilotGeometry[i], b = PilotGeometry[i + 1], c = PilotGeometry[i + 7], d = PilotGeometry[i + 8];
			if (min(min(a.w, b.w), min(c.w, d.w)) > -1e5 && max(max(a.w, b.w), max(c.w, d.w)) > -.2) {
				float2 ab = b.xy - a.xy, ac = c.xy - a.xy, db = b.xy - d.xy, dc = c.xy - d.xy;
				float area = .5 * (abs(ab.x * ac.y - ab.y * ac.x) + abs(db.x * dc.y - db.y * dc.x));
				stats.xyz = float3(area, area * area, 1);
			}
		}
		if (lane < 49 && lane % 7 > 0 && lane % 7 < 6 && lane / 7 > 0 && lane / 7 < 6) {
			float4 c = PilotGeometry[lane], l = PilotGeometry[lane - 1], r = PilotGeometry[lane + 1], b = PilotGeometry[lane - 7], t = PilotGeometry[lane + 7];
			if (min(min(l.w, r.w), min(b.w, t.w)) > -1e5 && c.w > -.2) {
				float2 scale = .5 * float2(OutputWidth, OutputHeight);
				stats.w = max(length((l.xy + r.xy - 2 * c.xy) * scale), length((b.xy + t.xy - 2 * c.xy) * scale));
			}
		}
		BeamStatistics[lane] = stats;
		GroupMemoryBarrierWithGroupSync();
		[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
		{
			if (lane < stride) {
				BeamStatistics[lane].xyz += BeamStatistics[lane + stride].xyz;
				BeamStatistics[lane].w = max(BeamStatistics[lane].w, BeamStatistics[lane + stride].w);
			}
			GroupMemoryBarrierWithGroupSync();
		}
		float4 total = BeamStatistics[0];
		float variation = sqrt(max(total.y * total.z / max(total.x * total.x, 1e-20) - 1, 0));
		float desired = 7 * sqrt(max(total.w / 8, 0) / .35);
		desired = max(desired, 8 * sqrt(min(variation, 9)));
		// The coarse pilot cannot reliably discover a narrow transmitting lobe.
		// Retain the paper's 16-cell floor; 8-cell grids failed isolated-ghost
		// convergence even when the pilot's area variance appeared small.
		// Small two-cell steps avoid replacing a whole third of the lattice at a
		// single threshold while the source moves. All levels retain a center ray.
		uint cells = (uint)clamp(2 * ceil(desired * .5), 16, 32);
		grid = Grid > 33 ? Grid : min(cells + 1, Grid);
	}
	job.Padding = grid;
	if (lane == 0)
		OutJobs[group.y] = job;
	if (lane < 8) {
		float2 marginal = 0;
		float total = 0;
		[unroll] for (uint i = 0; i < 7; ++i)
		{
			uint left = lane > 0 ? lane - 1 : 0, right = min(lane, 6);
			marginal += .5 * float2(PilotWeights[i * 7 + left] + PilotWeights[i * 7 + right], PilotWeights[left * 7 + i] + PilotWeights[right * 7 + i]);
			[unroll] for (uint j = 0; j < 7; ++j) total += PilotWeights[i * 7 + j];
		}
		PupilPDF[lane] = total > 1e-20 ? .5 + .5 * marginal * 7 / total : 1;
	}
	GroupMemoryBarrierWithGroupSync();
	// Normalize the trapezoidal integral of the nodal PDF, including endpoints.
	for (uint coordinate = lane; coordinate < grid; coordinate += 64) {
		float2 integral = .5 * (PupilPDF[0] + PupilPDF[7]);
		[unroll] for (uint i = 1; i < 7; ++i) integral += PupilPDF[i];
		// Quantile accepts an integral in units of one uniform bin.
		float2 q = float(coordinate) / (grid - 1) * integral / 7;
		OutPupilCoordinates[group.y * Grid + coordinate] = PupilQuantile(q);
	}
	GroupMemoryBarrierWithGroupSync();
}

	// Compact only visible, transmitting paths. Rank by irradiance rather than
	// raw footprint: a huge, extremely dilute ghost must not crowd out the chain.
	// At most 24 jobs in total; no CPU readback or drawing empty sources.
	[numthreads(256, 1, 1)] void BuildJobs(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	Source source = Sources[group.x];
	if (!any(source.Color.rgb > 0))
		return;
	// A fixed total path budget bounds trace and raster cost when several
	// highlights are present. Keep one path per source, distribute the rest by
	// sqrt(flux); a single highlight retains all 24 original paths.
	float totalImportance = 0;
	uint activeSources = 0;
	for (uint sourceIndex = 0; sourceIndex < MaxSources; ++sourceIndex) {
		float flux = dot(Sources[sourceIndex].Color.rgb, float3(.2126, .7152, .0722));
		activeSources += flux > 0;
		totalImportance += sqrt(max(flux, 0));
	}
	float importance = sqrt(max(dot(source.Color.rgb, float3(.2126, .7152, .0722)), 0));
	float continuousQuota = 1 + (PathsPerSource - min(activeSources, PathsPerSource)) * importance / max(totalImportance, 1e-20);
	uint pathQuota = uint(continuousQuota);
	if (activeSources == 1)
		pathQuota = PathsPerSource;
	float2 slope = (.5 - source.Position.xy) * float2(SensorWidth, SensorHeight) / FocalLength;
	uint sortCount = 1u << (firstbithigh(max(PathCount, 2u) - 1u) + 1u);
	for (uint i = lane; i < sortCount; i += 256) {
		float score = 0;
		float4 bounds = 0;
		if (i < PathCount) {
			bounds = FitPupil(i, source.Position.xy);
			float2 extent = (bounds.zw - bounds.xy) * .5;
			float2 center = (bounds.xy + bounds.zw) * .5;
			float2 xy = Paths[i].SensorMap.x * center + Paths[i].SensorMap.y * slope;
			float2 size = abs(Paths[i].SensorMap.x) * extent;
			float2 sourceSize = 3 * abs(Paths[i].SensorMap.y / FocalLength) * sqrt(source.Position.zw) * float2(SensorWidth, SensorHeight);
			if (all(extent > 0) && all(abs(xy) - size - sourceSize < float2(SensorWidth, SensorHeight) * .55)) {
				float area = 4 * extent.x * extent.y;
				float sensorArea = max(4 * (size.x * size.y + sourceSize.x * sourceSize.y), SensorWidth * SensorHeight * .00002);
				if (BeamState > 0) {
					// Filter each axis before estimating peak irradiance. An area-only
					// floor assigns a finite score to an arbitrarily thin, long beam;
					// when its pupil closes, removing it moves the global cutoff abruptly.
					float2 filtered = max(size + sourceSize, .5 * float2(SensorWidth, SensorHeight) / float2(OutputWidth, OutputHeight));
					sensorArea = max(4 * filtered.x * filtered.y, SensorWidth * SensorHeight * .00002);
				}
				score = lerp(Paths[i].SensorMap.z, Paths[i].SensorMap.w, Coating) * area / sensorArea;
			}
		}
		PathScores[i] = score;
		PathOrder[i] = i;
		if (i < PathCount)
			PathBounds[i] = bounds;
	}
	GroupMemoryBarrierWithGroupSync();
	// Sort once instead of synchronizing a separate 64-lane reduction for
	// each of the 24 winners. Preserve score ordering and lowest-index ties.
	// One thread owns both ends of a comparison, so each stage needs one barrier.
	[loop] for (uint span = 2; span <= sortCount; span <<= 1)
	{
		[loop] for (uint stride = span >> 1; stride > 0; stride >>= 1)
		{
			for (uint pair = lane; pair < sortCount / 2; pair += 256) {
				uint a = (pair / stride) * (2 * stride) + pair % stride;
				uint b = a + stride;
				float sa = PathScores[a], sb = PathScores[b];
				uint ia = PathOrder[a], ib = PathOrder[b];
				bool aFirst = sa > sb || (sa == sb && ia < ib);
				if (aFirst != ((a & span) == 0)) {
					PathScores[a] = sb;
					PathScores[b] = sa;
					PathOrder[a] = ib;
					PathOrder[b] = ia;
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}
	if (lane == 0) {
		PickCount = 0;
		LastScore = 0;
		for (uint pick = 0; pick < min(pathQuota, PathCount) && PathScores[pick] > 0; ++pick) {
			PickCount = pick + 1;
			LastScore = PathScores[pick];
		}
		Arguments.InterlockedAdd(4, PickCount, JobBase);
		uint unused;
		Arguments.InterlockedAdd(16, PickCount, unused);
		Arguments.InterlockedAdd(36, PickCount, unused);
		Arguments.InterlockedAdd(52, PickCount, unused);
	}
	if (BeamState > 0 && lane == 0) {
		// The visible-job budget is global (24); unused job slots hold one
		// source cutoff each. A disappearing narrow candidate must not instantly
		// brighten every remaining ghost by lowering their common fade threshold.
		float4 configuration = float4(FNumber, FocalLength, Coating, PathCount);
		[branch] if (BeamState > 1)
		{
			Job history = OutJobs[PathsPerSource + group.x];
			float2 previousUV = asfloat(uint2(history.SourceIndex, history.PathIndex));
			if (all(history.Bounds == configuration) && length(previousUV - source.Position.xy) < .05)
				LastScore = max(LastScore, history.Weight * CutoffRelease);
		}
		Job history = (Job)0;
		history.Bounds = configuration;
		history.SourceIndex = asuint(source.Position.x);
		history.PathIndex = asuint(source.Position.y);
		history.Weight = LastScore;
		OutJobs[PathsPerSource + group.x] = history;
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane < PickCount) {
		Job job;
		job.Bounds = PathBounds[PathOrder[lane]];
		job.SourceIndex = group.x;
		job.PathIndex = PathOrder[lane];
		job.Weight = 1;
		job.Padding = 0;
		// Fade near the budget cutoff so swapping the weakest path cannot pop.
		if (PickCount == PathsPerSource)
			job.Weight = smoothstep(LastScore, LastScore * 2, PathScores[lane]);
		// When flux crosses a quota boundary, a newly allocated path starts at
		// zero instead of appearing at full strength. Preserve the mandatory path.
		else if (PickCount == pathQuota && pathQuota > 1 && lane + 1 == PickCount)
			job.Weight = smoothstep(0, 1, frac(continuousQuota));
		OutJobs[JobBase + lane] = job;
#ifdef PHYSICAL_LENS_AREA_LAYERS
		// Bound each coarse draw by its last contributing job. Point-only
		// frames leave all three counts zero; no CPU readback or extra pass.
		if (job.Weight > 0) {
			Source light = Sources[job.SourceIndex];
			float magnification = Paths[job.PathIndex].SensorMap.y / FocalLength;
			[unroll] for (uint layer = 1; layer <= 3; ++layer)
			{
				if (max(AreaLayerWeight(light, magnification, layer, false), AreaLayerWeight(light, magnification, layer, true)) > 0) {
					uint unused;
					Arguments.InterlockedMax(68 + 20 * (layer - 1), JobBase + lane + 1, unused);
				}
			}
		}
#endif
	}
}

// Invert a continuous piecewise-linear PDF. The uniform mixture preserves
// support even when coarse probes miss a narrow transmitting lobe.
float2 PupilQuantile(float2 q)
{
	float2 remaining = q * 7, result = 1;
	[unroll] for (uint i = 0; i < 7; ++i)
	{
		float2 a = PupilPDF[i], b = PupilPDF[i + 1];
		float2 mass = .5 * (a + b);
		float2 t = 2 * max(remaining, 0) / max(a + sqrt(max(a * a + 2 * (b - a) * max(remaining, 0), 0)), 1e-8);
		result = float2(remaining.x >= 0 && remaining.x <= mass.x ? (i + saturate(t.x)) / 7 : result.x,
			remaining.y >= 0 && remaining.y <= mass.y ? (i + saturate(t.y)) / 7 : result.y);
		remaining -= mass;
	}
	return result;
}

[numthreads(64, 1, 1)] void TraceGhosts(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	Job job = Jobs[group.y];
	Source source = Sources[job.SourceIndex];
	float wavelengths[3] = { .650, .550, .450 };
	uint grid = (uint)job.Padding;
	uint i = group.x * 64 + lane;
	if (i >= grid * grid)
		return;
	uint2 xy = uint2(i % grid, i / grid);
	float2 coordinate = float2(PupilCoordinates[group.y * Grid + xy.x].x, PupilCoordinates[group.y * Grid + xy.y].y);
	float2 lo = float2(PupilCoordinates[group.y * Grid + (xy.x > 0 ? xy.x - 1 : 0)].x, PupilCoordinates[group.y * Grid + (xy.y > 0 ? xy.y - 1 : 0)].y);
	float2 hi = float2(PupilCoordinates[group.y * Grid + min(xy.x + 1, grid - 1)].x, PupilCoordinates[group.y * Grid + min(xy.y + 1, grid - 1)].y);
	float2 width = (hi - lo) * .5;
	float2 pupil = lerp(job.Bounds.xy, job.Bounds.zw, coordinate);
	float4 ray = Trace(pupil, source.Position.xy, Paths[job.PathIndex].Indices.xy, wavelengths[group.z]);
	float2 step = width * float2(xy.x == 0 || xy.x == grid - 1 ? 2 : 1, xy.y == 0 || xy.y == grid - 1 ? 2 : 1);
	float coverage = saturate(ray.w / max(2 * max(step.x, step.y), 1e-6) + .5);
	float2 area = width * (job.Bounds.zw - job.Bounds.xy);
	// Store integrated energy, including the nonuniform quadrature weight.
	ray.z *= coverage * area.x * area.y / (PI * PupilRadius * PupilRadius);
	OutRays[(group.y * 3 + group.z) * Vertices + i] = ray;
}

	[numthreads(64, 1, 1)] void ReduceGhosts(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	Job job = Jobs[group.y];
	Source source = Sources[job.SourceIndex];
	uint grid = (uint)job.Padding;
	float3 moment = 0, square = 0;
	float2 pixels = float2(OutputWidth, OutputHeight);
	// Moments are relative to a paraxial center, avoiding loss of precision for
	// a subpixel caustic far from screen center.
	float2 slope = (.5 - source.Position.xy) * float2(SensorWidth, SensorHeight) / FocalLength;
	float2 center = (Paths[job.PathIndex].SensorMap.x * PupilToLens((job.Bounds.xy + job.Bounds.zw) * .5, source.Position.xy) + Paths[job.PathIndex].SensorMap.y * slope) / float2(SensorWidth, SensorHeight) * float2(-2, 2);
	for (uint i = lane; i < grid * grid; i += 64) {
		float4 ray = TracedRays[(group.y * 3 + group.z) * Vertices + i];
		float w = ray.z;
		float2 xyPixel = (ray.xy - center) * pixels * .5;
		moment += float3(xyPixel, 1) * w;
		square += float3(xyPixel * xyPixel, xyPixel.x * xyPixel.y) * w;
	}
	Moments[lane] = moment;
	Squares[lane] = square;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (lane < stride) {
			Moments[lane] += Moments[lane + stride];
			Squares[lane] += Squares[lane + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0) {
		float w = Moments[0].z;
		float2 mean = Moments[0].xy / max(w, 1e-20);
		float3 covariance = Squares[0] / max(w, 1e-20) - float3(mean * mean, mean.x * mean.y);
		float magnification = Paths[job.PathIndex].SensorMap.y / FocalLength;
		float opticalVariance = max(covariance.x + covariance.y, 0);
		float3 sourceCovariance = magnification * magnification * float3(source.Position.zw * pixels * pixels, -source.Color.w * pixels.x * pixels.y);
		covariance += sourceCovariance;
		covariance.xy = max(covariance.xy, 0) + .25;
		float limit = sqrt(covariance.x * covariance.y) * .999;
		covariance.z = clamp(covariance.z, -limit, limit);
		Bundle bundle;
		bundle.CenterFlux = float4(center + mean * 2 / pixels, w, 0);
		float flux = bundle.CenterFlux.z * source.Color[group.z] * Intensity * job.Weight;
		float peak = flux * OutputWidth * OutputHeight / (2 * PI * sqrt(max(covariance.x * covariance.y - covariance.z * covariance.z, 1e-12)));
		// Exposure-aware sub-display culling. A generous 16x caustic allowance
		// protects local peaks; fade out contributions below 1/65536 linear light.
		bundle.CenterFlux.w = saturate(peak * VisibilityExposure() * 16 * 65536 - 1);
		// When the emitter dominates the footprint, rasterize its convolved
		// moments as a normalized ellipse instead of drawing individual samples.
		float areaWeight = smoothstep(.25, 1, (sourceCovariance.x + sourceCovariance.y) / max(opticalVariance, .25));
		bundle.Covariance = float4(covariance, max(areaWeight, saturate((5 - sqrt(max(covariance.x, covariance.y))) * .5)));
		OutBundles[group.y * 3 + group.z] = bundle;
#ifdef PHYSICAL_LENS_COMPACT_BEAMS
		bool visible = job.Weight > 0 && bundle.CenterFlux.z > 0 && bundle.CenterFlux.w > 0;
		bool hasPoint = bundle.Covariance.w > 0;
		RasterQuadStart = hasPoint ? 0 : 1;
		RasterCount = visible ? uint(hasPoint) + (bundle.Covariance.w < 1 ? 2 * (grid - 1) * (grid - 1) : 0) : 0;
		RasterArguments.InterlockedAdd(group.z * 20 + 4, RasterCount, RasterFirst);
#endif
	}
#ifdef PHYSICAL_LENS_COMPACT_BEAMS
	GroupMemoryBarrierWithGroupSync();
	uint bandStart = group.z * (1 + 2 * (Grid - 1) * (Grid - 1)) * PathsPerSource;
	for (uint q = lane; q < RasterCount; q += 64)
		OutRasterQuads[bandStart + RasterFirst + q] = (group.y << 14) | (RasterQuadStart + q);
#endif
}
