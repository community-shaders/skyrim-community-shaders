// Persistent snow deformation map update.
//
// The map is a square world-space window following the camera in whole-texel
// steps. Each frame the previous map is re-read at a scrolled offset, snow
// refill is applied, and this frame's actor stamps are max-blended in.
// Texel value = normalized depression depth (0 = untouched, 1 = ground).

#define MAX_STAMPS 128

cbuffer PerFrame : register(b0)
{
	float2 WindowOrigin;
	int2 ScrollDelta;

	float TexelSize;
	uint StampCount;
	float RefillAmount;
	uint ClearMap;

	float4 Stamps[MAX_STAMPS];     // xy: world pos, z: depth, w: radius
	float4 StampEnds[MAX_STAMPS];  // xy: previous world pos (capsule segment start)
}

Texture2D<float> PreviousDeformation : register(t0);
RWTexture2D<float> CurrentDeformation : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	uint2 pixel = DTid.xy;

	float deformation = 0.0;

	if (!ClearMap) {
		int2 sourcePixel = int2(pixel) + ScrollDelta;

		uint2 dims;
		PreviousDeformation.GetDimensions(dims.x, dims.y);

		[branch] if (all(sourcePixel >= 0) && all(sourcePixel < int2(dims)))
		{
			deformation = PreviousDeformation[uint2(sourcePixel)];
		}

		deformation = max(deformation - RefillAmount, 0.0);
	}

	float2 worldPos = WindowOrigin + (float2(pixel) + 0.5) * TexelSize;

	for (uint i = 0; i < StampCount; i++) {
		// Capsule stamp: distance to the segment from the actor's previous
		// position, so trails are continuous regardless of movement speed.
		float2 p0 = StampEnds[i].xy;
		float2 p1 = Stamps[i].xy;
		float2 seg = p1 - p0;
		float segLenSq = dot(seg, seg);
		float t = segLenSq > 1e-4 ? saturate(dot(worldPos - p0, seg) / segLenSq) : 0.0;
		float2 delta = worldPos - (p0 + seg * t);
		float distSq = dot(delta, delta);
		float radius = Stamps[i].w;

		[branch] if (distSq < radius * radius)
		{
			// Smooth falloff starting at 0.2 of the radius: trails get soft,
			// wide borders instead of hard-walled circles, and the wide band
			// keeps trench walls representable for coarser consumers of the
			// map (grid-based geometry sampling it at lower resolution).
			float falloff = 1.0 - smoothstep(0.2, 1.0, sqrt(distSq) / radius);
			deformation = max(deformation, Stamps[i].z * falloff);
		}
	}

	CurrentDeformation[pixel] = deformation;
}
