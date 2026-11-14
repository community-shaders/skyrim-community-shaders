#ifndef PHYSICAL_SKY_CLOUD_PHASE_HLSLI
#define PHYSICAL_SKY_CLOUD_PHASE_HLSLI

uint2 CloudPhaseOffset(uint frameIndex)
{
	static const uint order[16] = { 0u, 10u, 2u, 8u, 5u, 15u, 7u, 13u, 1u, 11u, 3u, 9u, 4u, 14u, 6u, 12u };
	const uint phase = order[frameIndex & 15u];
	return uint2(phase & 3u, phase >> 2u);
}

#endif
