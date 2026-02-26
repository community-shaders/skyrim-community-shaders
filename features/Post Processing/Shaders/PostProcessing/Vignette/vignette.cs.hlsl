RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);

cbuffer VignetteCB : register(b1)
{
	float4 Params0;  // focal, anamorphism (included in aspect ratio), power, aspect ratio
	float4 RcpDynRes;
};

[numthreads(8, 8, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	float3 color = TexColor[tid].rgb;

	float2 uv = (tid + .5) * RcpDynRes.xy;

	float cos_view = length((uv - .5) * float2(1, Params0.w));
	cos_view = Params0.x * rsqrt(cos_view * cos_view + Params0.x * Params0.x);
	float vignette = pow(cos_view, Params0.z);

	color *= vignette;

	RWTexOut[tid] = float4(color, 1);
}