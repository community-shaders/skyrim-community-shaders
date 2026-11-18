#define PHYSICAL_LENS_PLAN_BAKE 1
#include "PostProcessing/PhysicalLensFlare/flare.cs.hlsl"
RWStructuredBuffer<float4> PlanOutput : register(u5);
StructuredBuffer<float4> PlanTable : register(t6);
RWStructuredBuffer<uint> PlanStatus : register(u0);
RWStructuredBuffer<uint> PlanRequests : register(u4);
StructuredBuffer<uint> RequestedPlans : register(t0);
#ifdef PHYSICAL_LENS_PLAN_GRID_CAP
static const uint PlanningGridCap = PHYSICAL_LENS_PLAN_GRID_CAP;
#else
static const uint PlanningGridCap = Grid;
#endif

float PlanAngle(float2 uv)
{
	return saturate(length((.5 - uv) * float2(SensorWidth, SensorHeight)) /
					(.5 * length(float2(SensorWidth, SensorHeight)))) *
	       64;
}

// At most four entries per selected path, shared across sources. Request,
// bake and lookup are separate dispatches; no cross-group spin or CPU readback.
[numthreads(4, 1, 1)] void RequestPlans(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	Job job = Jobs[group.y];
	uint a = min(uint(floor(PlanAngle(Sources[job.SourceIndex].Position.xy))), 63);
	uint row = uint(clamp(int(a) + int(lane) - 1, 0, 64));
	uint entry = row * PathCount + job.PathIndex;
	uint previous;
	InterlockedCompareExchange(PlanStatus[entry], 0, 1, previous);
	if (previous == 0) {
		uint slot;
		Arguments.InterlockedAdd(0, 1, slot);
		PlanRequests[slot] = entry;
	}
}

void BakePlan(uint path, uint row, uint lane)
{
	float maximumSlope = .5 * length(float2(SensorWidth, SensorHeight)) / FocalLength;
	Source source = (Source)0;
	source.Position.xy = float2(.5 - (float(row) / 64) * maximumSlope * FocalLength / SensorWidth, .5);
	Job job = (Job)0;
	job.PathIndex = path;
	job.Bounds = FitPupil(path, source.Position.xy);
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
	if (PlanningGridCap > 33)
		grid = PlanningGridCap;
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
	float2 samplingError = 0;
	float irradianceEstimate = 0;
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
		samplingError = float2(variation, total.w);
#ifdef PHYSICAL_LENS_PLAN_DIAGNOSTICS
		if (lane == 0) {
			float transmitted = 0;
			for (uint probe = 0; probe < 49; ++probe)
				transmitted += PilotGeometry[probe].z * saturate(PilotGeometry[probe].w * 3 + .5);
			float2 extent = job.Bounds.zw - job.Bounds.xy;
			float coatingScale = lerp(Paths[path].SensorMap.z, Paths[path].SensorMap.w, Coating) / max(Paths[path].SensorMap.z, 1e-20);
			float flux = transmitted / 49 * extent.x * extent.y / (PI * PupilRadius * PupilRadius) * coatingScale;
			irradianceEstimate = flux * flux / max(total.x * 49 / 36, 4 / (OutputWidth * OutputHeight));
		}
#endif
		float desired = 7 * sqrt(max(total.w / 8, 0) / .35);
		desired = max(desired, 8 * sqrt(min(variation, 9)));
		// The coarse pilot cannot reliably discover a narrow transmitting lobe.
		// Retain the paper's 16-cell floor; 8-cell grids failed isolated-ghost
		// convergence even when the pilot's area variance appeared small.
		// Small two-cell steps avoid replacing a whole third of the lattice at a
		// single threshold while the source moves. All levels retain a center ray.
		uint cells = (uint)clamp(2 * ceil(desired * .5), 16, 32);
		grid = PlanningGridCap > 33 ? PlanningGridCap : min(cells + 1, PlanningGridCap);
	}
	job.Padding = grid;
	if (lane == 0) {
		PlanOutput[(row * PathCount + path) * 6] = job.Bounds;
#ifdef PHYSICAL_LENS_PLAN_DIAGNOSTICS
		PlanOutput[(row * PathCount + path) * 6 + 1] = float4(grid, samplingError, irradianceEstimate);
#else
		PlanOutput[(row * PathCount + path) * 6 + 1] = float4(grid, 0, 0, 0);
#endif
	}
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
	if (lane < 4)
		PlanOutput[(row * PathCount + path) * 6 + 2 + lane] = float4(PupilPDF[lane * 2], PupilPDF[lane * 2 + 1]);
}

[numthreads(64, 1, 1)] void BakeRequestedPlans(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	uint entry = RequestedPlans[group.x];
	BakePlan(entry % PathCount, entry / PathCount, lane);
}
	// Offline reference also verifies the request cache against a complete table.
	[numthreads(64, 1, 1)] void BakePlans(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	BakePlan(group.x, group.y, lane);
}

float4 PlanBounds(uint row, uint path)
{
	return PlanTable[(row * PathCount + path) * 6];
}
float4 BoundUnion(float4 a, float4 b) { return float4(min(a.xy, b.xy), max(a.zw, b.zw)); }
[numthreads(64, 1, 1)] void ExpandCachedJobs(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	Job job = OutJobs[group.y];
	Source source = Sources[job.SourceIndex];
	float angle = PlanAngle(source.Position.xy);
	uint a = min((uint)floor(angle), 63), b = a + 1;
	uint before = a > 0 ? a - 1 : 0, after = min(b + 1, 64);
	float t = angle - a;
	float4 middle = BoundUnion(PlanBounds(a, job.PathIndex), PlanBounds(b, job.PathIndex));
	// Each endpoint encloses its two neighbours. Continuous interpolation
	// consequently encloses both sampled ends of the current angular interval.
	job.Bounds = lerp(BoundUnion(PlanBounds(before, job.PathIndex), middle), BoundUnion(middle, PlanBounds(after, job.PathIndex)), t);
	float ga = PlanTable[(a * PathCount + job.PathIndex) * 6 + 1].x;
	float gb = PlanTable[(b * PathCount + job.PathIndex) * 6 + 1].x;
	float4 original = lerp(PlanBounds(a, job.PathIndex), PlanBounds(b, job.PathIndex), t);
	float2 expansion = (job.Bounds.zw - job.Bounds.xy) / max(original.zw - original.xy, 1e-5);
	float cells = (max(ga, gb) - 1) * max(expansion.x, expansion.y);
	uint grid = Reserved > 1 ? Grid : min(uint(2 * ceil(cells * .5) + 1), PlanningGridCap);
#ifdef PHYSICAL_LENS_REFERENCE_GRID
	grid = PHYSICAL_LENS_REFERENCE_GRID;
#endif
	job.Padding = grid;
	if (lane == 0)
		OutJobs[group.y] = job;
	if (lane < 4) {
		float4 pdf = lerp(PlanTable[(a * PathCount + job.PathIndex) * 6 + 2 + lane], PlanTable[(b * PathCount + job.PathIndex) * 6 + 2 + lane], t);
		PupilPDF[lane * 2] = pdf.xy;
		PupilPDF[lane * 2 + 1] = pdf.zw;
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint coordinate = lane; coordinate < grid; coordinate += 64) {
		float2 integral = .5 * (PupilPDF[0] + PupilPDF[7]);
		[unroll] for (uint i = 1; i < 7; ++i) integral += PupilPDF[i];
		OutPupilCoordinates[group.y * Grid + coordinate] = PupilQuantile(float(coordinate) / (grid - 1) * integral / 7);
	}
}
