#ifndef PHYSICAL_SKY_CLOUD_MOTION_HLSLI
#define PHYSICAL_SKY_CLOUD_MOTION_HLSLI

float2 CloudFieldWarp(float2 positionMeters, float2 evolution)
{
	if (evolution.y == 0.0)
		return 0.0;
	const float2 phase = float2(dot(positionMeters, float2(0.0007, 0.0004)),
							 dot(positionMeters, float2(-0.0003, 0.0009))) +
	                     evolution.x * float2(1, -2) + float2(0, 2);
	const float2 waves = sin(phase);
	return evolution.y * float2(80.0 * waves.x + 40.0 * waves.y, 60.0 * waves.y - 30.0 * waves.x);
}

float2 CloudFieldPosition(float2 positionMeters, float2 evolution)
{
	return positionMeters - CloudFieldWarp(positionMeters, evolution);
}

float2 CloudPreviousFieldOffset(float2 positionMeters, float2 fieldShiftMeters, float4 evolution)
{
	if (all(evolution.xy == evolution.zw) && all(fieldShiftMeters == 0.0))
		return 0.0;
	const float2 offset = fieldShiftMeters - CloudFieldWarp(positionMeters, evolution.xy);
	float2 delta = 0.0;
	[unroll] for (uint i = 0u; i < 2u; ++i)
		delta = offset + CloudFieldWarp(positionMeters + delta, evolution.zw);
	return delta;
}

#endif
