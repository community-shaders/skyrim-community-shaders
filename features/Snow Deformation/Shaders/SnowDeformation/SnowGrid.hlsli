#define kWarpBands 5
static const float kWarpBandVerts[kWarpBands] = { 192.0, 8.0, 8.0, 8.0, 104.0 };
static const float kWarpBandMul[kWarpBands] = { 1.0, 2.0, 4.0, 8.0, 16.0 };

float3 WarpBand(float au)
{
	float3 found = float3(kWarpBandMul[kWarpBands - 1], 1.0, (float)(kWarpBands - 1));
	bool done = false;
	float acc = 0.0;
	[unroll] for (int band = 0; band < kWarpBands; ++band)
	{
		float prev = acc;
		acc += kWarpBandVerts[band];
		[flatten] if (!done && au < acc)
		{
			found = float3(kWarpBandMul[band], saturate((au - prev) / max(kWarpBandVerts[band], 1.0)), (float)band);
			done = true;
		}
	}
	return found;
}

float InverseWarpAxis(float w)
{
	float a = abs(w) / GridSpacing;
	float u = 0.0;
	[unroll] for (int band = 0; band < kWarpBands; ++band)
	{
		float span = kWarpBandVerts[band] * kWarpBandMul[band];
		float take = min(a, span);
		u += take / kWarpBandMul[band];
		a -= take;
	}
	u += a / kWarpBandMul[kWarpBands - 1];
	return sign(w) * u;
}
