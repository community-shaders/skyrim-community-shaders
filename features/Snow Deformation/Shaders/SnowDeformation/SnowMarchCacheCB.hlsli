StructuredBuffer<uint> MarchTilesIn : register(t63);
cbuffer MarchCacheCB : register(b2)
{
	uint MarchRowStart;
	uint MarchRowCount;
	uint2 MarchCachePad;
#ifdef SNOW_MARCH_SUN_CS
	float4 MarchSun;
#endif
}
