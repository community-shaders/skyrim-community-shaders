#ifndef PHYSICAL_SKY_DISK_PHASE_HLSLI
#define PHYSICAL_SKY_DISK_PHASE_HLSLI

// Symmetric fourth-root cosine mapping resolves both narrow forward and backward lobes.
static const uint DISK_PHASE_LUT_SIZE = 2048u;
Texture2D<float4> TexDiskPhaseLut : register(t106);

float4 SampleDiskPhase(float cosine, uint lightIndex)
{
	const float mu = clamp(cosine, -1.0, 1.0);
	const float edge = 0.5 * sqrt(sqrt(1.0 - abs(mu)));
	const float coordinate = (mu >= 0.0 ? edge : 1.0 - edge) * (DISK_PHASE_LUT_SIZE - 1u);
	const uint index = min(uint(coordinate), DISK_PHASE_LUT_SIZE - 2u);
	return lerp(TexDiskPhaseLut.Load(int3(index, lightIndex, 0)), TexDiskPhaseLut.Load(int3(index + 1u, lightIndex, 0)), coordinate - index);
}

uint CloudDiskIndex()
{
	return SharedData::physSkyData.volCloudUseSun != 0u ? 0u : SharedData::physSkyData.dirLightDiskIndex;
}

// Exact second moment of a uniform spherical cap, divided by its solid angle.
float DiskRayleigh(float cosine, uint lightIndex)
{
	const float capCos = cos(SharedData::physSkyData.diskAngularRadius[lightIndex]);
	const float secondMoment = capCos * (1.0 + capCos) * 0.5;
	return (1.0 + 0.25 * secondMoment * (3.0 * cosine * cosine - 1.0)) / (4.0 * Math::PI);
}

#endif
