#include "ProceduralGrass/PGrassCommon.hlsli"

struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float BladeHeight: TEXCOORD0;
};

void main(PS_INPUT input)
{
	// Keep the original triangles in both passes; hardware clipping changes depth interpolation.
	clip(grassHiZBounds.w < 0.0f ? 1.0f : input.BladeHeight - grassHiZBounds.w);
}
