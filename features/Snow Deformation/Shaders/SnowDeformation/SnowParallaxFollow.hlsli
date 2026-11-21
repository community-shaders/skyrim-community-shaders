#ifndef SNOW_DEFORMATION_SNOWPARALLAXFOLLOW_HLSLI
#define SNOW_DEFORMATION_SNOWPARALLAXFOLLOW_HLSLI

namespace SnowParallaxFollow
{
	float3 UVShiftToWorld(float2 uvShift, float2 uvDx, float2 uvDy, float3 posDx, float3 posDy)
	{
		const float det = uvDx.x * uvDy.y - uvDy.x * uvDx.y;
		const float2 px = float2(uvDy.y * uvShift.x - uvDy.x * uvShift.y, uvDx.x * uvShift.y - uvDx.y * uvShift.x) / det;
		return det * det > 1e-6 * dot(uvDx, uvDx) * dot(uvDy, uvDy) ? posDx * px.x + posDy * px.y : 0.0;
	}
}

#endif
