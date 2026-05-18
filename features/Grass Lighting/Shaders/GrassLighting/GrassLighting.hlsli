namespace GrassLighting
{
	float3 GetLightSpecularInput(float3 L, float3 V, float3 N, float3 lightColor, float shininess)
	{
		float3 H = normalize(V + L);
		float HdotN = saturate(dot(H, N));
		if (SharedData::enbSettings.Enable)
			shininess *= SharedData::enbSettings.ColorPow;	
		float lightColorMultiplier = exp2(shininess * log2(HdotN));
		return lightColor * lightColorMultiplier;
	}

	float3 TransformNormal(float3 normal)
	{
		return normal * 2.0 - 1.0;
	}

	// http://www.thetenthplanet.de/archives/1180
	float3x3 CalculateTBN(float3 N, float3 p, float2 uv)
	{
		// get edge vectors of the pixel triangle
		float3 dp1 = ddx_coarse(p);
		float3 dp2 = ddy_coarse(p);
		float2 duv1 = ddx_coarse(uv);
		float2 duv2 = ddy_coarse(uv);

		// solve the linear system
		float3 dp2perp = cross(dp2, N);
		float3 dp1perp = cross(N, dp1);
		float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

		// construct a scale-invariant frame
		float invmax = rsqrt(max(dot(T, T), dot(B, B)));
		return float3x3(T * invmax, B * invmax, N);
	}
}
