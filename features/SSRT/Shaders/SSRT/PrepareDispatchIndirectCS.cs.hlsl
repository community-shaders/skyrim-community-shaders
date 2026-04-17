// Prepare indirect dispatch arguments from empty tile count
// Writes (ceil(count/64), 1, 1) to the indirect args buffer

#include "SSRT/GI1Common.hlsli"

StructuredBuffer<uint> g_EmptyTileCount : register(t0);
RWByteAddressBuffer g_IndirectArgs : register(u0);

[numthreads(1, 1, 1)] void main()
{
	uint count = g_EmptyTileCount[0];
	uint groupsX = (count + 63) / 64;

	g_IndirectArgs.Store(0, groupsX);
	g_IndirectArgs.Store(4, 1);
	g_IndirectArgs.Store(8, 1);
}
