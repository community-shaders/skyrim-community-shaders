#include "PostProcessing/PhysicalLensFlare/optics.hlsli"
#ifdef PHYSICAL_LENS_AREA_LAYERS
#	include "PostProcessing/PhysicalLensFlare/area.hlsli"
#endif
StructuredBuffer<float4> Rays : register(t0);
#ifdef PHYSICAL_LENS_COMPACT_BEAMS
StructuredBuffer<uint> RasterQuads : register(t6);
#endif
StructuredBuffer<Source> Sources : register(t1);
StructuredBuffer<Job> Jobs : register(t3);
StructuredBuffer<Bundle> Bundles : register(t4);
Texture2D<float4> ApertureField : register(t5);
SamplerState ApertureSampler : register(s0);
#ifdef PHYSICAL_LENS_GATHER
static const uint CoarsePass = 0;
#else
cbuffer GhostChannel : register(b5)
{
	uint RenderChannel, CoarsePass;
	float2 ChannelPadding;
}
#endif
static const uint RayPlane = PathsPerSource * 3 * Vertices;
#ifdef PHYSICAL_LENS_GATHER
struct Vertex
{
	float4 Position;
	float Valid;
	float3 Aperture;
	float Irradiance;
	float2 Kernel;
	float2 PointCoverage;
	float3 WaveMip;
	float2 BoxSigma;
	float2 ClipSigma;
	float4 ExtraApertures;
	float4 CellEdges;
	float2 ExtraStops;
	float VioletGain;
};
#else
struct Vertex
{
	float4 Position: SV_Position;
	float Valid: SV_ClipDistance0;
	float3 Aperture: TEXCOORD0;
	float Irradiance: COLOR0;
	float2 Kernel: TEXCOORD1;
	nointerpolation float2 PointCoverage: TEXCOORD2;
	nointerpolation float3 WaveMip: TEXCOORD3;
	nointerpolation float2 BoxSigma: TEXCOORD4;
	nointerpolation float2 ClipSigma: TEXCOORD5;
	float4 ExtraApertures: TEXCOORD6;
	float4 CellEdges: TEXCOORD9;
	nointerpolation float2 ExtraStops: TEXCOORD7;
	nointerpolation float VioletGain: TEXCOORD8;
};
#endif

float Cross(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
#ifdef PHYSICAL_LENS_GATHER
#	define RETURN_BEAM return
void BeamVertex(uint vertex, uint instance, uint band, uint cellStep, out Vertex output)
#else
#	ifdef PHYSICAL_LENS_AREA_LAYERS
#		define RETURN_BEAM                                                              \
			{                                                                            \
				output.Position.xy = AreaRasterPosition(output.Position.xy, CoarsePass); \
				return output;                                                           \
			}
#	else
#		define RETURN_BEAM return output
#	endif
Vertex VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
#endif
{
#ifndef PHYSICAL_LENS_GATHER
	uint band = RenderChannel;
	uint cellStep = 1;
#endif
#ifdef PHYSICAL_LENS_COMPACT_BEAMS
	uint packed = RasterQuads[band * (1 + 2 * (Grid - 1) * (Grid - 1)) * PathsPerSource + instance];
	instance = packed >> 14;
	uint quad = packed & 16383;
	if (quad > 0) {
		uint cells = uint(Jobs[instance].Padding) - 1;
		uint localCell = (quad - 1) / 2;
		uint paddedCell = (localCell / cells) * (Grid - 1) + localCell % cells;
		vertex += 4 + paddedCell * 8 + ((quad - 1) % 2) * 4;
	}
#endif
#ifndef PHYSICAL_LENS_GATHER
	Vertex output;
#endif
	output = (Vertex)0;
	output.Position.w = 1;
	output.Valid = -1;
	Job job = Jobs[instance];
	output.VioletGain = band == 2 ? Paths[job.PathIndex].Spectrum[0].w : 0;
	uint bundleIndex = instance * 3 + band;
	Bundle bundle = Bundles[bundleIndex];
#ifdef PHYSICAL_LENS_AREA_LAYERS
	float areaWeight = AreaLayerWeight(Sources[job.SourceIndex], Paths[job.PathIndex].SensorMap.y / FocalLength, CoarsePass, vertex < 4);
	if (job.Weight <= 0)
#else
	float areaWeight = 1;
	if (CoarsePass != 0 || job.Weight <= 0)
#endif
		RETURN_BEAM;
	if (vertex < 4) {
		if (areaWeight <= 0)
			RETURN_BEAM;
		if (bundle.Covariance.w <= 0 || bundle.CenterFlux.z <= 0)
			RETURN_BEAM;
		GhostPath path = Paths[job.PathIndex];
		float2 spectral = GhostSpectralRMS(path, Bundles[instance * 3].CenterFlux.xy, Bundles[instance * 3 + 1].CenterFlux.xy,
							  Bundles[instance * 3 + 2].CenterFlux.xy, band) *
		                  .5 * float2(OutputWidth, OutputHeight);
		float3 cov = bundle.Covariance.xyz + float3(spectral * spectral, spectral.x * spectral.y);
		float a = sqrt(max(cov.x, .25)), b = cov.z / a, d = sqrt(max(cov.y - b * b, .25));
		float2 quad[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
		output.Kernel = 3 * quad[vertex];
		float2 offset = float2(a * output.Kernel.x, b * output.Kernel.x + d * output.Kernel.y);
		output.Position = float4(bundle.CenterFlux.xy + 2 * offset / float2(OutputWidth, OutputHeight), 0, 1);
		output.Valid = 1;
		output.PointCoverage = float2(-3, 1);
		float flux = Sources[job.SourceIndex].Color[band] * Intensity * job.Weight * bundle.CenterFlux.z * bundle.Covariance.w * bundle.CenterFlux.w;
		output.Irradiance = flux * OutputWidth * OutputHeight / (2 * PI * a * d * (1 - 5.5 * exp(-4.5)));
		output.Irradiance *= areaWeight;
		RETURN_BEAM;
	}
	uint grid = (uint)job.Padding;
	uint cell = (vertex - 4) / 8;
	bool pointCopy = ((vertex - 4) / 4) % 2 != 0;
	areaWeight = pointCopy ? areaWeight : (CoarsePass == 0 ? 1 : 0);
	if (areaWeight <= 0)
		RETURN_BEAM;
	uint2 xy = uint2(cell % (Grid - 1), cell / (Grid - 1));
	if (any(xy + cellStep >= grid) || bundle.Covariance.w >= 1 || bundle.CenterFlux.w <= 0)
		RETURN_BEAM;
	uint base = (instance * 3 + band) * Vertices;
	uint start = base + xy.y * grid + xy.x;
	uint indices[4] = { start, start + cellStep, start + grid * cellStep, start + grid * cellStep + cellStep };
	float4 hits[4], powers[4];
	bool valid = true;
	[unroll] for (uint i = 0; i < 4; ++i)
	{
		hits[i] = Rays[indices[i]];
		powers[i] = Rays[indices[i] + 2 * RayPlane];
		valid = valid && hits[i].w > -1e5 && all(isfinite(hits[i]));
	}
	if (!valid)
		RETURN_BEAM;
	uint corner = (vertex - 4) % 4;
	uint index = indices[corner];
	float4 nodal[4];
	[unroll] for (uint node = 0; node < 4; ++node) nodal[node] = Rays[indices[node] + 3 * RayPlane];
	float4 filterWeight = float4(nodal[0].w, nodal[1].w, nodal[2].w, nodal[3].w);
	if (pointCopy ? all(filterWeight == 0) : all(filterWeight == 1))
		RETURN_BEAM;
	float pupilArea = abs((powers[3].z - powers[0].z) * (powers[3].w - powers[0].w));
	float transmission = .25 * (powers[0].x + powers[1].x + powers[2].x + powers[3].x);
	// ShadeBeams already conserves power across the connected triangle mesh.
	// Keep this shared nodal value identical on both sides of every edge.
	float density = nodal[corner].x * (1 - nodal[corner].w);
	output.Position = float4(hits[corner].xy, 0, 1);
	output.Valid = 1;
	output.Aperture = Rays[index + RayPlane].xyz;
	float4 stopB = Rays[index + 4 * RayPlane], stopC = Rays[index + 5 * RayPlane];
	output.ExtraApertures = float4(stopB.xy, stopC.xy);
	output.ExtraStops = float2(stopB.w, stopC.w);
	output.Irradiance = Sources[job.SourceIndex].Color[band] * Intensity * job.Weight * density;
	float2 pixelScale = .5 * float2(OutputWidth, OutputHeight);
	float2 dx = .5 * (hits[1].xy - hits[0].xy + hits[3].xy - hits[2].xy) * pixelScale;
	float2 dy = .5 * (hits[2].xy - hits[0].xy + hits[3].xy - hits[1].xy) * pixelScale;
	float3 covariance = float3(dx * dx + dy * dy, dx.x * dx.y + dy.x * dy.y) / 12;
	float minor = .5 * (covariance.x + covariance.y - sqrt(max(pow(covariance.x - covariance.y, 2) + 4 * covariance.z * covariance.z, 0)));
	float2 spectral = .25 * (Rays[indices[0] + 3 * RayPlane].yz + Rays[indices[1] + 3 * RayPlane].yz + Rays[indices[2] + 3 * RayPlane].yz + Rays[indices[3] + 3 * RayPlane].yz);
	Source source = Sources[job.SourceIndex];
	float magnification = Paths[job.PathIndex].SensorMap.y / FocalLength;
	float2 pixels = float2(OutputWidth, OutputHeight);
	float3 blur = magnification * magnification * float3(source.Position.zw * pixels * pixels, -source.Color.w * pixels.x * pixels.y);
	blur += float3(spectral * spectral, spectral.x * spectral.y);
	// Integrate resolved cells against the emitter/spectral/pixel footprint;
	// genuinely collapsed cells retain the point reconstruction of section 7.2.
	if (pointCopy) {
		float kernelPeak = 1;
		float2 cellX = dx, cellY = dy;
		float determinant = Cross(cellX, cellY);
		bool integrateCell = minor > .01 && abs(determinant) > .01;
		// A cell is a continuous beam, not a delta at its center. Approximating
		// its box by a narrow Gaussian leaves a periodic lattice in smooth ghosts.
		// Use the actual shared quad edges, not an affine replacement of each
		// quad: independently approximated quads leave gaps on nonlinear maps.
		// Only truly collapsed cells use a point kernel (Hullin section 7.2).
		if (!integrateCell)
			covariance *= 3;
		covariance += blur;
		covariance.xy += .25;
		float a = sqrt(covariance.x), b = covariance.z / a;
		float d = sqrt(max(covariance.y - b * b, .25));
		float2 quad[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
		float2 q = 3 * quad[corner];
		float2 center = .25 * (hits[0].xy + hits[1].xy + hits[2].xy + hits[3].xy);
		float2 offset = float2(a * q.x, b * q.x + d * q.y);
		output.Position.xy = center + offset / pixelScale;
		output.Kernel = q;
		if (integrateCell) {
			float2 rowX = float2(cellY.y, -cellY.x) / determinant;
			float2 rowY = float2(-cellX.y, cellX.x) / determinant;
			float3 filter = blur + float3(.25, .25, 0);
			float2 sigma = sqrt(max(float2(dot(rowX * rowX, filter.xy) + 2 * rowX.x * rowX.y * filter.z,
										dot(rowY * rowY, filter.xy) + 2 * rowY.x * rowY.y * filter.z),
				1e-8));
			output.BoxSigma = sigma;
			float2 warp = .25 * (hits[3].xy - hits[2].xy - hits[1].xy + hits[0].xy) * pixelScale;
			float2 padding = abs(float2(dot(rowX, warp), dot(rowY, warp)));
			// Bound the peak of the filtered cell, including its non-affine warp.
			// A fixed absolute kernel cutoff would erase broad, dilute cells.
			float2 peak = min(1, (1 + 2 * padding) * .39894228 / sigma);
			kernelPeak = peak.x * peak.y;
			output.Kernel = quad[corner] * (.5 + padding + 4 * sigma);
			output.Position.xy = center + (cellX * output.Kernel.x + cellY * output.Kernel.y) / pixelScale;
			const uint starts[4] = { 0, 3, 1, 2 }, ends[4] = { 2, 1, 0, 3 };
			// Opposite sides of a shared edge must use the same filter width.
			// A cell-average spectrum makes their CDFs non-complementary, exposing
			// the mesh even when incident irradiance is perfectly smooth.
			[unroll] for (uint edge = 0; edge < 4; ++edge)
			{
				float2 a = hits[starts[edge]].xy * pixelScale, b = hits[ends[edge]].xy * pixelScale;
				float2 normal = sign(determinant) * float2(b.y - a.y, a.x - b.x);
				float2 edgeSpectral = .5 * (Rays[indices[starts[edge]] + 3 * RayPlane].yz + Rays[indices[ends[edge]] + 3 * RayPlane].yz);
				float3 edgeFilter = magnification * magnification * float3(source.Position.zw * pixels * pixels, -source.Color.w * pixels.x * pixels.y) + float3(edgeSpectral * edgeSpectral, edgeSpectral.x * edgeSpectral.y) + float3(.25, .25, 0);
				float deviation = sqrt(max(dot(normal * normal, edgeFilter.xy) + 2 * normal.x * normal.y * edgeFilter.z, 1e-12));
				output.CellEdges[edge] = dot(output.Position.xy * pixelScale - a, normal) / (1.41421356237 * deviation);
			}
		}
		float4 a0 = Rays[indices[0] + RayPlane], a1 = Rays[indices[1] + RayPlane];
		float4 a2 = Rays[indices[2] + RayPlane], a3 = Rays[indices[3] + RayPlane];
		output.Aperture = .25 * (a0.xyz + a1.xyz + a2.xyz + a3.xyz);
		float irisCenter = .25 * (a0.w + a1.w + a2.w + a3.w);
		float irisWidth = .5 * (abs(a1.w - a0.w) + abs(a2.w - a0.w) + abs(a3.w - a1.w) + abs(a3.w - a2.w));
		float glassWidth = .5 * (abs(a1.z - a0.z) + abs(a2.z - a0.z) + abs(a3.z - a1.z) + abs(a3.z - a2.z));
		output.PointCoverage = float2(saturate(irisCenter / max(irisWidth, 1e-6) + .5), saturate(output.Aperture.z / max(glassWidth, 1e-6) + .5));
		// Filtered cells evaluate glass coverage continuously in the pixel
		// shader, so their unused constant-coverage channel carries the peak.
		if (integrateCell)
			output.PointCoverage.y = kernelPeak;
		// Negative X distinguishes the point footprint without another varying.
		output.PointCoverage.x = -1 - output.PointCoverage.x;
		// The unresolved footprint integrates the aperture texture too. It must
		// not sample one texel and retain fringes smaller than a source/pixel.
		float2 uvExtent = abs(a1.xy - a0.xy) + abs(a2.xy - a0.xy);
		float mappedArea = max(abs(Cross(dx, dy)), .25);
		float blurRatio = sqrt(max(blur.x + blur.y, 0) / mappedArea);
		output.WaveMip = max(log2(max(max(uvExtent.x, uvExtent.y) * 128 * max(1, blurRatio), 1)), 0);
		if (integrateCell) {
			float4 du = .5 * (a1 - a0 + a3 - a2), dv = .5 * (a2 - a0 + a3 - a1);
			// Continuous late clipping is essential even on filtered cells. A
			// single coverage/texture value per beam turns curved rims into steps.
			output.Aperture += du.xyz * output.Kernel.x + dv.xyz * output.Kernel.y;
			output.ClipSigma = sqrt(du.wz * du.wz * output.BoxSigma.x * output.BoxSigma.x + dv.wz * dv.wz * output.BoxSigma.y * output.BoxSigma.y);
			float2 uvFilter = abs(du.xy) * output.BoxSigma.x + abs(dv.xy) * output.BoxSigma.y;
			output.WaveMip = max(log2(max(max(uvFilter.x, uvFilter.y) * 256, 1)), 0);
		}
		[branch] if (any(output.ExtraStops > 0))
		{
			[unroll] for (uint crossing = 0; crossing < 2; ++crossing)
			{
				uint plane = (4 + crossing) * RayPlane;
				float2 p0 = Rays[indices[0] + plane].xy, p1 = Rays[indices[1] + plane].xy;
				float2 p2 = Rays[indices[2] + plane].xy, p3 = Rays[indices[3] + plane].xy;
				float2 value = .25 * (p0 + p1 + p2 + p3);
				if (integrateCell)
					value += .5 * (p1 - p0 + p3 - p2) * output.Kernel.x + .5 * (p2 - p0 + p3 - p1) * output.Kernel.y;
				float2 filterExtent = integrateCell ?
				                          abs(.5 * (p1 - p0 + p3 - p2)) * output.BoxSigma.x + abs(.5 * (p2 - p0 + p3 - p1)) * output.BoxSigma.y :
				                          .5 * (abs(p1 - p0) + abs(p2 - p0)) * max(1, blurRatio);
				output.WaveMip[crossing + 1] = max(log2(max(max(filterExtent.x, filterExtent.y) * 256, 1)), 0);
				if (crossing == 0)
					output.ExtraApertures.xy = value;
				else
					output.ExtraApertures.zw = value;
			}
		}
		float flux = transmission * pupilArea / (PI * PupilRadius * PupilRadius) * dot(filterWeight, .25);
		output.Irradiance = Sources[job.SourceIndex].Color[band] * Intensity * job.Weight * flux * OutputWidth * OutputHeight /
		                    (2 * PI * a * d * (1 - 5.5 * exp(-4.5)));
		if (integrateCell) {
			float d0 = nodal[0].x * nodal[0].w, d1 = nodal[1].x * nodal[1].w;
			float d2 = nodal[2].x * nodal[2].w, d3 = nodal[3].x * nodal[3].w;
			// Preserve the continuous nodal field through the filter footprint.
			// Per-cell constant flux turns smooth irradiance gradients into tiles.
			float smoothDensity = .25 * (d0 + d1 + d2 + d3) + .5 * (d1 - d0 + d3 - d2) * output.Kernel.x + .5 * (d2 - d0 + d3 - d1) * output.Kernel.y;
			output.Irradiance = Sources[job.SourceIndex].Color[band] * Intensity * job.Weight * max(smoothDensity, 0);
		}
	}
	output.Irradiance *= areaWeight * (1 - bundle.Covariance.w) * bundle.CenterFlux.w;
	RETURN_BEAM;
}
#undef RETURN_BEAM

float2 Erf(float2 x)
{
	float2 t = rcp(1 + .3275911 * abs(x));
	float2 p = (((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - .284496736) * t + .254829592;
	return sign(x) * (1 - p * t * exp(-x * x));
}
float ApertureClearance(float2 p)
{
	float sector = 2 * PI / Blades;
	float angle = atan2(p.y, abs(p.x) + abs(p.y) > 1e-8 ? p.x : 1e-8) - Rotation;
	float delta = angle - sector * floor(angle / sector + .5);
	float2 local = length(p) * float2(cos(delta), sin(delta));
	float apothem = cos(PI / Blades), sag = (1 - apothem) * BladeCurvature;
	float edge = 2 * sin(PI / Blades);
	float clearance = 1 - (local.x + 4 * sag * local.y * local.y / (edge * edge)) / (apothem + sag);
	if (BladeCurvature < 0)
		clearance = CameraIrisClearance(p, Blades, Rotation, BladeCurvature);
	return clearance;
}
struct GhostColor
{
	float Band: SV_Target0;
	float Violet: SV_Target1;
};

#ifndef PHYSICAL_LENS_GATHER
float ShadeGhost(Vertex input)
{
	bool isPoint = input.PointCoverage.x < 0;
	bool isBox = input.BoxSigma.x > 0;
	float weight = 1;
	if (isPoint) {
		if (input.BoxSigma.x > 0) {
			// At this distance Erf rounds to one in float32. Interior pixels
			// need neither four exponentials nor the boundary reconstruction.
			[branch] if (all(input.CellEdges >= 4))
				weight = 1;
			else
			{
				float2 integrated = .5 * float2(dot(Erf(input.CellEdges.xy), float2(1, 1)), dot(Erf(input.CellEdges.zw), float2(1, 1)));
				weight = max(integrated.x, 0) * max(integrated.y, 0);
			}
		} else {
			float r2 = dot(input.Kernel, input.Kernel);
			clip(9 - r2);
			weight = exp(-.5 * r2) - exp(-4.5);
		}
	}
	// Skip negligible convolution tails before aperture texture reads and
	// blending. Broad area layers tolerate a looser relative error; the fine
	// layer retains the sharper limit. Neither the iris nor caustic is blurred.
	float cutoff = (isBox ? input.PointCoverage.y : 1) * (CoarsePass > 0 ? 1e-3 : 1e-4);
	clip(weight > cutoff ? 1 : -1);
	float2 p = input.Aperture.xy;
	float clearance = ApertureClearance(p);
	if (input.ExtraStops.x > 0)
		clearance = min(clearance, ApertureClearance(input.ExtraApertures.xy));
	if (input.ExtraStops.y > 0)
		clearance = min(clearance, ApertureClearance(input.ExtraApertures.zw));
	float iris = saturate(clearance / max(fwidth(clearance), 1e-6) + .5);
	if (isPoint && !isBox)
		iris = saturate(-input.PointCoverage.x - 1);
	if (isBox && input.ClipSigma.x > 1e-6)
		iris = .5 + .5 * Erf(float2(clearance / (1.41421356237 * input.ClipSigma.x), 0)).x;
	[branch] if (GhostDiffraction > 0 && input.PointCoverage.x > -2.5)
	{
		float3 wave = isPoint ? ApertureField.SampleLevel(ApertureSampler, p * .25 + .5, input.WaveMip.x).rgb : ApertureField.Sample(ApertureSampler, p * .25 + .5).rgb;
		// Each pass clips the transmitted field. Keep its continuous mapping;
		// the minimum-envelope FrFT fit approximates the limiting pupil. This
		// does not claim a full coherent multi-plane propagation between stops.
		if (input.ExtraStops.x > 0)
			wave = min(wave, isPoint ? ApertureField.SampleLevel(ApertureSampler, input.ExtraApertures.xy * .25 + .5, input.WaveMip.y).rgb : ApertureField.Sample(ApertureSampler, input.ExtraApertures.xy * .25 + .5).rgb);
		if (input.ExtraStops.y > 0)
			wave = min(wave, isPoint ? ApertureField.SampleLevel(ApertureSampler, input.ExtraApertures.zw * .25 + .5, input.WaveMip.z).rgb : ApertureField.Sample(ApertureSampler, input.ExtraApertures.zw * .25 + .5).rgb);
		float value = RenderChannel == 0 ? wave.r : RenderChannel == 1 ? wave.g :
		                                                                 wave.b;
		iris = lerp(iris, value, GhostDiffraction);
	}
	float glass = saturate(input.Aperture.z / max(fwidth(input.Aperture.z), 1e-6) + .5);
	if (isBox && input.ClipSigma.y > 1e-6)
		glass = .5 + .5 * Erf(float2(input.Aperture.z / (1.41421356237 * input.ClipSigma.y), 0)).x;
	if (isPoint && !isBox)
		glass = input.PointCoverage.y;
	// Raster interpolation may undershoot zero at a vanishing-density vertex.
	// Radiance is nonnegative; leave the HDR upper range completely unbounded.
	return max(input.Irradiance, 0) * iris * glass * weight;
}
float PSMain(Vertex input) :
	SV_Target0
{
	return ShadeGhost(input);
}
GhostColor PSViolet(Vertex input)
{
	GhostColor output;
	output.Band = ShadeGhost(input);
	// Keep the short-wave red response at its blue beam position instead of
	// inventing an intermediate position by averaging separated spectral lobes.
	output.Violet = output.Band * input.VioletGain;
	return output;
}

float4 PSArea(Vertex input) : SV_Target0
{
	float value = ShadeGhost(input);
	return RenderChannel == 0 ? float4(value, 0, 0, 0) : RenderChannel == 1 ? float4(0, value, 0, 0) :
	                                                                          float4(value * input.VioletGain, 0, value, 0);
}

float2 PSPackedViolet(Vertex input) :
	SV_Target0
{
	float value = ShadeGhost(input);
	return float2(value, value * input.VioletGain);
}

#endif
