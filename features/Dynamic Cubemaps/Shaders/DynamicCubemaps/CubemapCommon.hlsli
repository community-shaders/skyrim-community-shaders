#ifndef DYNAMIC_CUBEMAPS_CUBEMAP_COMMON_HLSLI
#define DYNAMIC_CUBEMAPS_CUBEMAP_COMMON_HLSLI

float3 GetCubemapSamplingVector(uint3 texel, float2 size)
{
	float2 st = (texel.xy + 0.5) / size;
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	float3 direction = 0.0;
	switch (texel.z) {
	case 0:
		direction = float3(1.0, uv.y, -uv.x);
		break;
	case 1:
		direction = float3(-1.0, uv.y, uv.x);
		break;
	case 2:
		direction = float3(uv.x, 1.0, -uv.y);
		break;
	case 3:
		direction = float3(uv.x, -1.0, uv.y);
		break;
	case 4:
		direction = float3(uv.x, uv.y, 1.0);
		break;
	case 5:
		direction = float3(-uv.x, uv.y, -1.0);
		break;
	}
	return normalize(direction);
}

#endif
