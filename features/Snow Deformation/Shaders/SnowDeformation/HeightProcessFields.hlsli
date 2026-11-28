int2 ScrollDelta;
uint ClearAll;
uint pad0;

float2 HeightWindowCenter;
float HeightHalfExtent;
float pad1;

float2 TerrainWindowOrigin;
float TerrainTexelSize;
uint TerrainDim;

// ScrollCS's ghost merge: the camera clip transform, to ask whether the surface that wrote a stale texel was still submitted this frame
row_major float4x4 GhostViewProj;
float4 GhostCameraPosAdjust;
