#ifndef PHYSICAL_SKY_STORM_DATA_HLSLI
#define PHYSICAL_SKY_STORM_DATA_HLSLI
struct CloudStormParcel
{
	float4 sphere;
	float4 rotation;
	float4 noiseOffset;
	float4 previousSphere;
	float4 previousRotation;
	float4 previousNoiseOffset;
	float4 flow;
};
struct CloudStormData
{
	float4 centerRadiusStrength;
	float4 altitudeMotion;
	float4 driftDensity;
	float4 emission;
	float4 animation;
	float4 generation;
	float4 ambientPhase;
	float4 msCalibration[49];
	CloudStormParcel parcels[8];
	float4 plumeLower;
	float4 plumeUpper;
};
#endif
