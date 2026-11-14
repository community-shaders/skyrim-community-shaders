#include "SnowDeformation/SnowCommon.hlsli"

namespace SnowDeformation
{
	Texture2D<float4> DeformationMap : register(t101);
	Texture2D<float4> HorizonSnowAlbedo : register(t33);
	Texture2D<float4> HorizonSnowNormal : register(t23);
	Texture2D<float> WaterWindow : register(t63);
	Texture2D<float4> HorizonSnowRmaos : register(t68);
	Texture2D<float4> BermField : register(t56);

	bool UnderWater(float3 absWorld)
	{
		float dim = SharedData::snowDeformationSettings.WaterWindowDim;
		float2 tf = (absWorld.xy - SharedData::snowDeformationSettings.WaterWindowOrigin) / max(SharedData::snowDeformationSettings.WaterWindowTexel, 1.0) - 0.5;
		float2 t0f = floor(tf);
		float2 f = tf - t0f;
		int2 t0 = int2(t0f);
		int2 t1 = t0 + 1;
		bool inside = dim > 0.5 && all(t0 >= 0) && all(t1 < int(dim));
		float level = -100000.0;
		[branch] if (inside)
		{
			int rows = int(dim) - 1;
			float s00 = WaterWindow.Load(int3(t0.x, rows - t0.y, 0));
			float s10 = WaterWindow.Load(int3(t1.x, rows - t0.y, 0));
			float s01 = WaterWindow.Load(int3(t0.x, rows - t1.y, 0));
			float s11 = WaterWindow.Load(int3(t1.x, rows - t1.y, 0));
			float nearest = f.y < 0.5 ? (f.x < 0.5 ? s00 : s10) : (f.x < 0.5 ? s01 : s11);
			bool full = min(min(s00, s10), min(s01, s11)) > -50000.0;
			level = full ? lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y) : nearest;
		}
		return inside && absWorld.z < level - 2.0;
	}

	static const float SnowUVTile = 4096.0 / 24.0;

	float ClassifyLODSnow(float3 gammaColor)
	{
		SNOW_TEXEL_SCORE(score, gammaColor, 0.62 - 0.64 * saturate(SharedData::snowDeformationSettings.LODSnowSensitivity), 0.12);
		return score;
	}

	float2 GetDeformationUV(float2 absWorldXY)
	{
		return (absWorldXY - SharedData::snowDeformationSettings.WindowOrigin) * SharedData::snowDeformationSettings.InvWorldSize;
	}

	float DeformBilinear(float2 t, float2 mapDim)
	{
		t = clamp(t, 0.0, mapDim - 1.001);
		int2 t0 = (int2)t;
		float2 f = t - t0;
		int2 t1 = min(t0 + 1, int2(mapDim) - 1);

		const int2 mask = int2(mapDim) - 1;
		const int2 origin = SharedData::snowDeformationSettings.DeformMapOrigin;
		int2 q0 = (t0 + origin) & mask;
		int2 q1 = (t1 + origin) & mask;

		float s00 = DeformationMap.Load(int3(q0.x, q0.y, 0)).x;
		float s10 = DeformationMap.Load(int3(q1.x, q0.y, 0)).x;
		float s01 = DeformationMap.Load(int3(q0.x, q1.y, 0)).x;
		float s11 = DeformationMap.Load(int3(q1.x, q1.y, 0)).x;

		return saturate(lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y));
	}

	float GetDeformation(float2 absWorldXY)
	{
		float2 uv = GetDeformationUV(absWorldXY);

		float2 edge = min(uv, 1.0 - uv);
		float border = saturate(min(edge.x, edge.y) * 16.0);

		float deformation = 0.0;
		[branch] if (border > 0.0)
		{
			float2 mapDim;
			DeformationMap.GetDimensions(mapDim.x, mapDim.y);
			SNOW_DEFORM_BSPLINE(DeformBilinear, uv, mapDim);
			deformation = bspline * border;
		}
		return deformation;
	}

	float GetCutDate(float2 absWorldXY)
	{
		float2 mapDim;
		BermField.GetDimensions(mapDim.x, mapDim.y);
		float2 t = clamp(GetDeformationUV(absWorldXY) * mapDim - 0.5, 0.0, mapDim - 1.001);
		int2 t0 = (int2)t;
		float2 f = t - t0;
		int2 t1 = min(t0 + 1, int2(mapDim) - 1);

		const int2 mask = int2(mapDim) - 1;
		const int2 origin = SharedData::snowDeformationSettings.DeformMapOrigin;
		int2 q0 = (t0 + origin) & mask;
		int2 q1 = (t1 + origin) & mask;

		float2 s00 = BermField.Load(int3(q0.x, q0.y, 0)).yz;
		float2 s10 = BermField.Load(int3(q1.x, q0.y, 0)).yz;
		float2 s01 = BermField.Load(int3(q0.x, q1.y, 0)).yz;
		float2 s11 = BermField.Load(int3(q1.x, q1.y, 0)).yz;
		float2 v = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
		return v.x > 1e-5 ? v.y / v.x : 0.0;
	}
}
