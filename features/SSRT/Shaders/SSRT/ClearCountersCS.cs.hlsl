// Clear atomic counter buffers to zero

RWStructuredBuffer<uint> g_EmptyTileCount : register(u0);
RWStructuredBuffer<uint> g_OverrideTileCount : register(u1);

[numthreads(1, 1, 1)] void main()
{
	g_EmptyTileCount[0] = 0;
	g_OverrideTileCount[0] = 0;
}
