// Snow deformation sampling for the lighting pixel shader.
//
// The deformation map is a toroidal world-space window around the camera,
// updated by DeformationUpdateCS. Texel value is normalized depression depth:
// 0 = untouched snow, 1 = compressed to ground.

namespace SnowDeformation
{
	Texture2D<float> DeformationMap : register(t101);

	// Must match kTextureDim in src/Features/SnowDeformation.h.
	static const float MapDim = 2048.0;

	float2 GetDeformationUV(float2 absWorldXY)
	{
		return (absWorldXY - SharedData::snowDeformationSettings.WindowOrigin) * SharedData::snowDeformationSettings.InvWorldSize;
	}

	// Bilinear at fractional logical texel coordinates. Loads, not a sampler:
	// the map is toroidal, and a sampler would filter across the physical wrap
	// seam. Clamped logically first (the map border is the window border),
	// then each tap is masked to its physical texel.
	float DeformBilinear(float2 t)
	{
		t = clamp(t, 0.0, MapDim - 1.001);
		const int2 t0 = (int2)t;
		const float2 f = t - t0;
		const int2 t1 = min(t0 + 1, int(MapDim) - 1);

		const int2 mask = int(MapDim) - 1;
		const int2 origin = SharedData::snowDeformationSettings.MapOrigin;
		const int2 q0 = (t0 + origin) & mask;
		const int2 q1 = (t1 + origin) & mask;

		const float s00 = DeformationMap.Load(int3(q0.x, q0.y, 0));
		const float s10 = DeformationMap.Load(int3(q1.x, q0.y, 0));
		const float s01 = DeformationMap.Load(int3(q0.x, q1.y, 0));
		const float s11 = DeformationMap.Load(int3(q1.x, q1.y, 0));
		return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
	}

	float GetDeformation(float2 absWorldXY)
	{
		float2 uv = GetDeformationUV(absWorldXY);

		// Fade near the window border so the effect never pops at the edge.
		float2 edge = min(uv, 1.0 - uv);
		float border = saturate(min(edge.x, edge.y) * 16.0);

		float deformation = 0.0;
		[branch] if (border > 0.0)
		{
			// B-spline bicubic via 4 bilinear taps: value- and gradient-
			// continuous, so normals derived from this field do not band
			// per texel.
			float2 t = uv * MapDim - 0.5;
			float2 i = floor(t);
			float2 f = t - i;
			float2 f2 = f * f;
			float2 f3 = f2 * f;

			float2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
			float2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
			float2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
			float2 w3 = f3 / 6.0;

			float2 g0 = w0 + w1;
			float2 g1 = w2 + w3;
			float2 h0 = i - 1.0 + w1 / g0;
			float2 h1 = i + 1.0 + w3 / g1;

			float s00 = DeformBilinear(float2(h0.x, h0.y));
			float s10 = DeformBilinear(float2(h1.x, h0.y));
			float s01 = DeformBilinear(float2(h0.x, h1.y));
			float s11 = DeformBilinear(float2(h1.x, h1.y));

			deformation = (g0.y * (g0.x * s00 + g1.x * s10) + g1.y * (g0.x * s01 + g1.x * s11)) * border;
		}
		return deformation;
	}

	// Snow share of a landscape pixel's layer weights.
	float LandSnowness(float4 blendWeights1, float2 blendWeights2)
	{
#if defined(TRUE_PBR)
		// PBR terrain replaces the vanilla per-layer snow constants, so the
		// CPU side publishes per-tile snow-material bits via the permutation
		// data (see SnowDeformation::BSLightingShader_SetupMaterial).
		uint snowTileBits = (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowLandIsSnowMask) >> Permutation::ExtraFeatureFlags::SnowLandIsSnowShift;
		float4 snowIsSnow1to4 = float4(snowTileBits & 1, (snowTileBits >> 1) & 1, (snowTileBits >> 2) & 1, (snowTileBits >> 3) & 1);
		float2 snowIsSnow5to6 = float2((snowTileBits >> 4) & 1, (snowTileBits >> 5) & 1);
		return saturate(dot(blendWeights1, snowIsSnow1to4) + dot(blendWeights2, snowIsSnow5to6));
#else
		return saturate(dot(blendWeights1, LandscapeTexture1to4IsSnow) + blendWeights2.x * LandscapeTexture5to6IsSnow.x + blendWeights2.y * LandscapeTexture5to6IsSnow.y);
#endif
	}

	// Diagnostic overlay: R = outside deformation window, G = raw deformation
	// sample, B = detected snowness. Takes the weights after EM height blending.
	void DebugLandOverlay(inout float3 baseColor, float2 worldXYRel, float4 blendWeights1, float2 blendWeights2)
	{
		[branch] if ((SharedData::snowDeformationSettings.DebugTerrainOverlay & 1) != 0)
		{
			float2 debugWorldXY = worldXYRel + FrameBuffer::CameraPosAdjust.xy;
			float2 debugUV = GetDeformationUV(debugWorldXY);
			float debugOutside = (all(debugUV > 0.0) && all(debugUV < 1.0)) ? 0.0 : 1.0;
			float debugDeformation = GetDeformation(debugWorldXY);
			baseColor = lerp(baseColor, float3(debugOutside, debugDeformation, LandSnowness(blendWeights1, blendWeights2)), 0.75);
		}
	}
}
