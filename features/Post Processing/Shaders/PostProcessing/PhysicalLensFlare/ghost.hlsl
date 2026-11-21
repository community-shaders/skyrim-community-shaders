#include "PostProcessing/PhysicalLensFlare/optics.hlsli"
StructuredBuffer<float4> Rays : register(t0);
StructuredBuffer<Source> Sources : register(t1);
StructuredBuffer<Job> Jobs : register(t3);
StructuredBuffer<Bundle> Bundles : register(t4);
Texture2D<float4> GhostFresnel : register(t5);
SamplerState EdgeSampler : register(s0);
cbuffer GhostChannel : register(b5)
{
	uint RenderChannel;
	uint CoarsePass;
	float2 ChannelPadding;
}
struct Vertex
{
	float4 Position: SV_Position;
	float Clip: SV_ClipDistance0;
	float EdgeClip: SV_ClipDistance1;
	float2 Edge: TEXCOORD1;
	nointerpolation float Correction: TEXCOORD2;
	float3 Wave: TEXCOORD3;
	float2 Pupil: TEXCOORD0;
	float Color: COLOR0;
};
float NormalCDF(float x)
{
	float z = abs(x) * .70710678118;
	float t = rcp(1 + .3275911 * z);
	float erf = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - .284496736) * t + .254829592) * t * exp(-z * z);
	return .5 + .5 * (x < 0 ? -erf : erf);
}
float Variance(float2 normal, float3 covariance)
{
	return max(dot(normal * normal, covariance.xy) + 2 * normal.x * normal.y * covariance.z, 0);
}
float FresnelResidual(float distance, float sigma)
{
	float result = 0;
	[branch] if (distance > -6 && distance < 10 && sigma < 4)
	{
		float2 uv = (float2((distance + 6) * (511. / 16), sqrt(max(sigma, 0) * .25) * 47) + .5) / float2(512, 48);
		float3 value = GhostFresnel.SampleLevel(EdgeSampler, uv, 0).rgb;
		result = RenderChannel == 0 ? value.r : RenderChannel == 1 ? value.g :
		                                                             value.b;
	}
	return result;
}
// Reconstruct an energy-carrying pupil sample as an elliptical Gaussian.
// Its covariance includes the mapped pupil cell, finite emitter, spectral band
// and pixel footprint. No triangle ever connects separate optical ray hits.
Vertex Splat(float2 center, float3 covariance, float flux, uint corner, float3 boundary, float edgeVariance, float waveWidth)
{
	Vertex result = (Vertex)0;
	result.Position = float4(0, 0, 0, 1);
	result.Clip = -1;
	// A broad kernel has no detail at the half-resolution pixel scale. Its
	// minor-axis sigma must exceed 6 pixels before using the 4x coarser mip.
	float discriminant = sqrt(max((covariance.x - covariance.y) * (covariance.x - covariance.y) + 4 * covariance.z * covariance.z, 0));
	float boundaryVariance = Variance(boundary.xy, covariance);
	// The local edge model requires a resolved, approximately straight edge.
	// Keep its support inside the reconstructed beam and suppress it for an
	// extended emitter. Caustic folds pass zero: they are not knife edges.
	waveWidth = min(waveWidth, .9 * sqrt(boundaryVariance));
	float waveAmount = GhostDiffraction * smoothstep(.25, 1, waveWidth);
	if (sqrt(edgeVariance) >= 4 * waveWidth)
		waveAmount = 0;
	// A broad Gaussian may still contain a sharp aperture or caustic boundary.
	// Only its narrow high-frequency residual needs the half-resolution layer.
	bool smoothBoundary = boundary.z > 3 * sqrt(boundaryVariance) || (edgeVariance >= 16 && waveAmount == 0);
	bool coarse = .5 * (covariance.x + covariance.y - discriminant) >= 36;
	bool correction = coarse && !smoothBoundary && CoarsePass == 0;
	if (coarse != (CoarsePass != 0) && !correction)
		return result;
	result.Clip = 1;
	covariance.xy = max(covariance.xy, .25);
	float limit = sqrt(covariance.x * covariance.y) * .999;
	covariance.z = clamp(covariance.z, -limit, limit);
	float a = sqrt(covariance.x), b = covariance.z / a;
	float d = sqrt(max(covariance.y - b * b, .25));
	float2 corners[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
	float2 q = corners[corner] * 3;
	float2 offset = float2(a * q.x, b * q.x + d * q.y);
	float sigma = sqrt(max(edgeVariance, .25));
	float coarseSigma = coarse && !smoothBoundary ? max(sigma, 4) : sigma;
	if (correction) {
		float2 tangent = float2(-boundary.y, boundary.x);
		// Bound only the intersection of the edge strip and Gaussian support.
		// The former full-width strip shaded mostly discarded pixels for large,
		// tilted caustics. Conditional covariance preserves the identical kernel.
		float normalVariance = max(Variance(boundary.xy, covariance), .25);
		float crossVariance = dot(tangent, float2(covariance.x * boundary.x + covariance.z * boundary.y,
											   covariance.z * boundary.x + covariance.y * boundary.y));
		float shear = crossVariance / normalVariance;
		float conditionalVariance = max(Variance(tangent, covariance) - crossVariance * shear, 0);
		float strip = max(4 * coarseSigma, waveAmount > 0 ? 10 * waveWidth : 0);
		float2 normalRange = clamp(float2(-1, 1) * strip - boundary.z, -3 * sqrt(normalVariance), 3 * sqrt(normalVariance));
		float closest = clamp(0, normalRange.x, normalRange.y);
		float extent = sqrt(max(9 - closest * closest / normalVariance, 0) * conditionalVariance);
		float2 tangentRange = shear * normalRange;
		tangentRange = float2(min(tangentRange.x, tangentRange.y) - extent, max(tangentRange.x, tangentRange.y) + extent);
		offset = tangent * lerp(tangentRange.x, tangentRange.y, corners[corner].x * .5 + .5) +
		         boundary.xy * lerp(normalRange.x, normalRange.y, corners[corner].y * .5 + .5);
		q.x = offset.x / a;
		q.y = (offset.y - b * q.x) / d;
	}
	result.Position = float4(center + 2 * offset / float2(OutputWidth, OutputHeight), 0, 1);
	float clearance = boundary.z + dot(boundary.xy, offset);
	result.Edge = clearance / float2(sigma, coarseSigma);
	result.EdgeClip = correction ? 1 : clearance + max(4 * coarseSigma, waveAmount > 0 ? 6 * waveWidth : 0);
	result.Correction = correction;
	float combinedSigma = sqrt(max(boundaryVariance + edgeVariance, .25));
	[branch] if (boundary.z < 4 * combinedSigma)
	{
		float normalization = NormalCDF(boundary.z / combinedSigma);
		if (waveAmount > 0)
			normalization += waveAmount * FresnelResidual(boundary.z / waveWidth, combinedSigma / waveWidth);
		flux /= max(normalization, .005);
	}
	// Only the fine layer carries ripples; its correction strip subtracts the
	// unchanged coarse Gaussian. No new full-screen composite work is required.
	result.Wave = float3(clearance / max(waveWidth, .01), sigma / max(waveWidth, .01), CoarsePass == 0 ? waveAmount : 0);
	result.Pupil = q;
	result.Color = flux * OutputWidth * OutputHeight / (2 * PI * a * d * (1 - 5.5 * exp(-4.5)));
	return result;
}
Vertex SplatVS(uint vertex, uint instance)
{
	Vertex rejected = (Vertex)0;
	rejected.Position = float4(0, 0, 0, 1);
	rejected.Clip = -1;
	uint channel = RenderChannel;
	uint corner = (vertex - 4) % 4, sample = (vertex - 4) / 4;
	Job job = Jobs[instance];
	Source source = Sources[job.SourceIndex];
	uint bundleIndex = instance * 3 + channel;
	Bundle bundle = Bundles[bundleIndex];
	if (job.Weight <= 0 || bundle.CenterFlux.w <= 0)
		return rejected;
	uint low = channel == 0 ? 0 : channel - 1, high = channel == 2 ? 2 : channel + 1;
	float2 pixels = float2(OutputWidth, OutputHeight);
	float flux = source.Color[channel] * Intensity * job.Weight * bundle.CenterFlux.w;
	if (bundle.Covariance.w >= 1)
		return rejected;
	uint grid = (uint)job.Padding;
	uint x = sample % Grid, y = sample / Grid;
	if (x >= grid || y >= grid)
		return rejected;
	uint base = bundleIndex * Vertices;
	float4 ray = Rays[base + y * grid + x];
	if (ray.w < -1e5 || !all(isfinite(ray)))
		return rejected;
	// This is the same quadrature used by the bundle moment pass. It carries
	// energy through a caustic without dividing by a vanishing signed Jacobian.
	if (ray.z <= 0)
		return rejected;
	float4 l = Rays[base + y * grid + (x > 0 ? x - 1 : x)];
	float4 r = Rays[base + y * grid + min(x + 1, grid - 1)];
	float4 b = Rays[base + (y > 0 ? y - 1 : y) * grid + x];
	float4 t = Rays[base + min(y + 1, grid - 1) * grid + x];
	bool hasL = x > 0 && l.w > -1e5, hasR = x + 1 < grid && r.w > -1e5;
	bool hasB = y > 0 && b.w > -1e5, hasT = y + 1 < grid && t.w > -1e5;
	float2 dx = ((hasR ? r.xy : ray.xy) - (hasL ? l.xy : ray.xy)) / max(float(hasR) + float(hasL), 1) * pixels * .5;
	float2 dy = ((hasT ? t.xy : ray.xy) - (hasB ? b.xy : ray.xy)) / max(float(hasT) + float(hasB), 1) * pixels * .5;
	// A half-cell reconstruction kernel overlaps neighboring samples smoothly;
	// 1/sqrt(12) would reproduce the grid as separated hot spots.
	float3 covariance = .36 * float3(dx * dx + dy * dy, dx.x * dx.y + dy.x * dy.y);
	// A mapped footprint degenerates to a thin spoke at a fold. Include the
	// mapped cell's quadratic variation and bound unresolved anisotropy.
	float2 dxx = ((hasL ? l.xy : ray.xy) + (hasR ? r.xy : ray.xy) - 2 * ray.xy) * pixels * .5;
	float2 dyy = ((hasB ? b.xy : ray.xy) + (hasT ? t.xy : ray.xy) - 2 * ray.xy) * pixels * .5;
	covariance += .0648 * float3(dxx * dxx + dyy * dyy, dxx.x * dxx.y + dyy.x * dyy.y);
	// Curvature supplies the finite fold footprint; avoid an isotropic
	// anisotropy floor, which would broaden its sharp normal direction.
	float3 boundary = float3(0, 0, 1e6);
	float waveWidth = 0;
	float2 clearanceStep = .5 * float2(r.w - l.w, t.w - b.w);
	float determinant = dx.x * dy.y - dx.y * dy.x;
	[branch] if (hasL && hasR && hasB && hasT && abs(ray.w) < 2 * length(clearanceStep) && determinant * determinant > .01 * dot(dx, dx) * dot(dy, dy))
	{
		float2 gradient = float2(dy.y * clearanceStep.x - dx.y * clearanceStep.y, dx.x * clearanceStep.y - dy.x * clearanceStep.x) / determinant;
		float gradientLength = length(gradient);
		if (gradientLength > 1e-6) {
			boundary = float3(gradient / gradientLength, ray.w / gradientLength);
			// Empirical propagation scale, inspired by Hullin 2011 eq. (5).
			// Pupil-relative width follows sqrt(lambda * f-number), then the
			// local optical mapping carries it to pixels. It does not scale FOV.
			float wavelength = .65 - .1 * channel;
			waveWidth = .12 * sqrt(.15 * (wavelength / .4) * (FNumber / 18)) / gradientLength;
		}
	}
	// A local fold has a one-sided image. Its quadratic extremum supplies a
	// boundary; a symmetric Gaussian alone spreads energy across that edge.
	float3 mapped = float3(dx * dx + dy * dy, dx.x * dx.y + dy.x * dy.y);
	float trace = mapped.x + mapped.y;
	[branch] if (boundary.z > 1e5 && determinant * determinant < .0196 * trace * trace && hasL && hasR && hasB && hasT)
	{
		float disc = sqrt(max((mapped.x - mapped.y) * (mapped.x - mapped.y) + 4 * mapped.z * mapped.z, 0));
		float eigenvalue = .5 * (trace - disc);
		float2 normal = abs(mapped.z) > 1e-8 ? normalize(float2(mapped.z, eigenvalue - mapped.x)) : (mapped.x < mapped.y ? float2(1, 0) : float2(0, 1));
		float2 tangent = float2(-normal.y, normal.x);
		float2 direction = float2(-dot(tangent, dy), dot(tangent, dx));
		direction /= max(length(direction), 1e-8);
		float slope = dot(normal, dx * direction.x + dy * direction.y);
		float curvature = dot(normal, dxx * direction.x * direction.x + dyy * direction.y * direction.y);
		if (abs(curvature) > .5 && abs(slope) < abs(curvature))
			boundary = float3(normal * (curvature > 0 ? 1 : -1), slope * slope / (2 * abs(curvature)));
	}
	float magnification = Paths[job.PathIndex].SensorMap.y / FocalLength;
	float3 blurCovariance = magnification * magnification * float3(source.Position.zw * pixels * pixels, -source.Color.w * pixels.x * pixels.y);
	float4 leftBand = Rays[(instance * 3 + low) * Vertices + y * grid + x];
	float4 rightBand = Rays[(instance * 3 + high) * Vertices + y * grid + x];
	float2 spectral = leftBand.w > -1e5 && rightBand.w > -1e5 ?
	                      (rightBand.xy - leftBand.xy) * pixels * .5 / (high - low) :
	                      0;
	blurCovariance += float3(spectral * spectral, spectral.x * spectral.y) / 12;
	float edgeVariance = Variance(boundary.xy, blurCovariance) + .25;
	covariance += blurCovariance;
	covariance.xy += .25;
	flux *= ray.z * (1 - bundle.Covariance.w);
	return Splat(ray.xy, covariance, flux, corner, boundary, edgeVariance, waveWidth);
}

Vertex VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
	if (vertex >= 4)
		return SplatVS(vertex, instance);
	Vertex result = (Vertex)0;
	result.Position = float4(2, 2, 0, 1);
	result.Clip = -1;
	Job job = Jobs[instance];
	Source source = Sources[job.SourceIndex];
	if (job.Weight <= 0)
		return result;
	uint bundleIndex = instance * 3 + RenderChannel;
	Bundle bundle = Bundles[bundleIndex];
	if (bundle.CenterFlux.w <= 0)
		return result;
	uint channel = RenderChannel;
	if (bundle.Covariance.w <= 0 || bundle.CenterFlux.z <= 0)
		return result;
	uint low = channel == 0 ? 0 : channel - 1, high = channel == 2 ? 2 : channel + 1;
	Bundle leftBand = Bundles[instance * 3 + low], rightBand = Bundles[instance * 3 + high];
	float2 spectral = leftBand.CenterFlux.z > 0 && rightBand.CenterFlux.z > 0 ?
	                      (rightBand.CenterFlux.xy - leftBand.CenterFlux.xy) * float2(OutputWidth, OutputHeight) * .5 / (high - low) :
	                      0;
	// RGB represents finite 100 nm bands. Linear dispersion across each band
	// contributes variance; it needs no additional optical traces.
	bundle.Covariance.xyz += float3(spectral * spectral, spectral.x * spectral.y) / 12;
	float flux = source.Color[channel] * bundle.CenterFlux.z * Intensity * job.Weight * bundle.Covariance.w * bundle.CenterFlux.w;
	return Splat(bundle.CenterFlux.xy, bundle.Covariance.xyz, flux, vertex, float3(0, 0, 1e6), 1, 0);
}

float PSMain(Vertex input) :
	SV_Target
{
	float radiusSquared = dot(input.Pupil, input.Pupil);
	clip(9 - radiusSquared);
	float weight = exp(-.5 * radiusSquared) - exp(-4.5);
	float coverage = 1;
	[branch] if (input.Edge.y < 4)
		coverage = NormalCDF(input.Edge.y);
	[branch] if (input.Correction > 0)
	{
		float sharpCoverage = 1;
		[branch] if (input.Edge.x < 4)
			sharpCoverage = NormalCDF(input.Edge.x);
		coverage = sharpCoverage - coverage;
	}
	[branch] if (input.Wave.z > 0)
		coverage += input.Wave.z * FresnelResidual(input.Wave.x, input.Wave.y);
	return input.Color * weight * coverage;
}
