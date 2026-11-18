#ifndef PHYSICAL_LENS_AREA
#define PHYSICAL_LENS_AREA

float SourceMinorSigma(Source source)
{
	float2 variance = source.Position.zw * float2(Width * Width, Height * Height);
	float covariance = source.Color.w * Width * Height;
	return sqrt(max(.5 * (variance.x + variance.y - length(float2(variance.x - variance.y, 2 * covariance))), 0));
}

float AreaLayerWeight(Source source, float magnification, uint layer, bool moment)
{
	float2 pixels = float2(OutputWidth, OutputHeight);
	float3 covariance = magnification * magnification * float3(source.Position.zw * pixels * pixels, -source.Color.w * pixels.x * pixels.y);
	float sigma = sqrt(max(.5 * (covariance.x + covariance.y - length(float2(covariance.x - covariance.y, 2 * covariance.z))), 0));
	// Broad extended emitters tolerate reduced raster detail. Preserve the
	// point-source limit even for paths with very high magnification. A compact
	// whole-ghost moment needs denser sampling than the overlapping beam field.
	float3 lower = moment ? float3(6, 24, 96) : float3(2, 8, 32);
	float3 upper = moment ? float3(8, 32, 128) : float3(3, 12, 48);
	float3 weights = smoothstep(.75, 2, SourceMinorSigma(source)) * smoothstep(lower, upper, sigma);
	return layer == 0 ? 1 - weights.x : layer == 1 ? weights.x - weights.y :
	                                layer == 2     ? weights.y - weights.z :
	                                                 weights.z;
}

float2 AreaRasterPosition(float2 position, uint layer)
{
	if (layer == 0)
		return position;
	float divisor = exp2(2 * layer);
	float2 size = float2(OutputWidth, OutputHeight);
	// Four texels of guard band retain light outside the screen during cubic
	// reconstruction. Odd dimensions use the same centered mapping as resolve.
	return position * size / (divisor * (max(floor(size / divisor), 1) + 8));
}
#endif
