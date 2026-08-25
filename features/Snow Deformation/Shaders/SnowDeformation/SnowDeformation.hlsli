// Snow deformation sampling for the lighting pixel shader.
//
// The deformation map is a world-space window (absolute coordinates) around
// the camera, produced each frame by DeformationUpdateCS. Texel value is
// normalized depression depth: 0 = untouched snow, 1 = compressed to ground.

namespace SnowDeformation
{
	Texture2D<float> DeformationMap : register(t101);

	// Must match kTextureDim in src/Features/SnowDeformation.h.
	static const float MapDim = 2048.0;

	float2 GetDeformationUV(float2 absWorldXY)
	{
		return (absWorldXY - SharedData::snowDeformationSettings.WindowOrigin) * SharedData::snowDeformationSettings.InvWorldSize;
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
			float2 h0 = (i + 0.5 - 1.0 + w1 / g0) / MapDim;
			float2 h1 = (i + 0.5 + 1.0 + w3 / g1) / MapDim;

			float s00 = DeformationMap.SampleLevel(SampColorSampler, float2(h0.x, h0.y), 0);
			float s10 = DeformationMap.SampleLevel(SampColorSampler, float2(h1.x, h0.y), 0);
			float s01 = DeformationMap.SampleLevel(SampColorSampler, float2(h0.x, h1.y), 0);
			float s11 = DeformationMap.SampleLevel(SampColorSampler, float2(h1.x, h1.y), 0);

			deformation = (g0.y * (g0.x * s00 + g1.x * s10) + g1.y * (g0.x * s01 + g1.x * s11)) * border;
		}
		return deformation;
	}
}
