#ifndef __SNOW_COMMON_HLSLI__
#define __SNOW_COMMON_HLSLI__

#define TERRAIN_WINDOW_TEXEL(a_t, a_dim)           \
	float2 t = a_t;                                \
	t = clamp(t, 0.0, (float)(a_dim - 1) - 0.001); \
	int2 t0 = (int2)t;                             \
	float2 f = t - t0;                             \
	int2 t1 = min(t0 + 1, int2(a_dim - 1, a_dim - 1))

#define SNOW_DEFORM_BSPLINE(tap, uv, dims)                   \
	float2 t = uv * dims - 0.5;                              \
	float2 i = floor(t);                                     \
	float2 f = t - i;                                        \
	float2 f2 = f * f;                                       \
	float2 f3 = f2 * f;                                      \
	float2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;       \
	float2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;           \
	float2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0; \
	float2 w3 = f3 / 6.0;                                    \
	float2 g0 = w0 + w1;                                     \
	float2 g1 = w2 + w3;                                     \
	float2 h0 = i - 1.0 + w1 / g0;                           \
	float2 h1 = i + 1.0 + w3 / g1;                           \
	float v00 = tap(float2(h0.x, h0.y), dims);               \
	float v10 = tap(float2(h1.x, h0.y), dims);               \
	float v01 = tap(float2(h0.x, h1.y), dims);               \
	float v11 = tap(float2(h1.x, h1.y), dims);               \
	float bspline = g0.y * (g0.x * v00 + g1.x * v10) + g1.y * (g0.x * v01 + g1.x * v11)

#define SNOW_TANGENT_FRAME(t, b, n)                                                 \
	float3 t = normalize(cross(float3(0.0, 1.0, 0.0), n) + float3(1e-5, 0.0, 0.0)); \
	float3 b = cross(n, t)

#define SNOW_TEXEL_SCORE(score, c, lo, span)                              \
	float luminance = dot(c, float3(0.2126, 0.7152, 0.0722));             \
	float saturation = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); \
	float lumLo = lo;                                                     \
	float score = smoothstep(lumLo, lumLo + span, luminance) * (1.0 - smoothstep(0.10, 0.22, saturation))

float TerrainWindowHeight(Texture2D<float4> a_window, float2 a_t, uint a_dim)
{
	TERRAIN_WINDOW_TEXEL(a_t, a_dim);

	float s00 = a_window.Load(int3(t0.x, t0.y, 0)).x;
	float s10 = a_window.Load(int3(t1.x, t0.y, 0)).x;
	float s01 = a_window.Load(int3(t0.x, t1.y, 0)).x;
	float s11 = a_window.Load(int3(t1.x, t1.y, 0)).x;

	return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}

#endif
