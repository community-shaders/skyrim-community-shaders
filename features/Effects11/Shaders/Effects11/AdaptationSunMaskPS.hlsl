Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer SunMaskParams : register(b0)
{
	row_major float4x4 CameraViewProj;
	row_major float4x4 CameraViewProjInverse;
	float4 SunDirection;
	float4 DynamicResolution;
	float4 MaskParams;
};

struct PS_INPUT
{
	float4 pos: SV_POSITION;
	float2 txcoord0: TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
	float2 uv = input.txcoord0;
	float4 color = SourceTexture.SampleLevel(LinearSampler, uv, 0);

	float2 ndc = float2(uv.x * DynamicResolution.z, 1.0 - uv.y * DynamicResolution.w) * 2.0 - 1.0;
	float4 positionMS = mul(CameraViewProjInverse, float4(ndc, 0.5, 1.0));
	float3 viewDirection = normalize(positionMS.xyz / positionMS.w);

	float cosTheta = dot(viewDirection, SunDirection.xyz);
	if (cosTheta <= MaskParams.x)
		return color;

	float3 tangent = viewDirection - SunDirection.xyz * cosTheta;
	float tangentLength = length(tangent);
	float3 fallbackAxis = abs(SunDirection.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	tangent = tangentLength > 1e-5 ? tangent / tangentLength : normalize(cross(SunDirection.xyz, fallbackAxis));
	float3 sampleDirection = SunDirection.xyz * MaskParams.y + tangent * MaskParams.z;

	float4 sampleCS = mul(CameraViewProj, float4(sampleDirection, 0.0));
	if (sampleCS.w <= 1e-5)
		return color;

	float2 sampleNDC = sampleCS.xy / sampleCS.w;
	float2 sampleUV = float2(sampleNDC.x * 0.5 + 0.5, 0.5 - sampleNDC.y * 0.5) * DynamicResolution.xy;
	return SourceTexture.SampleLevel(LinearSampler, saturate(sampleUV), 0);
}
