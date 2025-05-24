#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState depthSampler : register(s0);
SamplerState snowDiffuseSampler : register(s1);
SamplerState snowAlphaSpecSampler : register(s2);

Texture2D<float4> depthTex : register(t0);
Texture2D<float4> snowDiffuseTex : register(t1);
Texture2D<float4> snowAlphaSpecTex : register(t2);

cbuffer PerGeometry : register(b2)
{
	float4 g_SSSDarkColor : packoffset(c0);   // fSnowSSSColorR in x, fSnowSSSColorG in y, fSnowSSSColorB in z, fSnowSSSDarkColorIntensity in w
	float4 g_SSSParameters : packoffset(c1);  // fSnowSSSStrength in x, fSnowSSSDepthDiff in y
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	const float4 iterationParameters[] = { { 0.784728, 0.669086, 0.560479, 0 },
		{ 0.000051, 0.000185, 0.004717, -2.000000 },
		{ 0.000842, 0.002820, 0.019283, -1.280000 },
		{ 0.006437, 0.013100, 0.036390, -0.720000 },
		{ 0.020926, 0.035861, 0.082190, -0.320000 },
		{ 0.079380, 0.113491, 0.077180, -0.080000 },
		{ 0.079380, 0.113491, 0.077180, 0.080000 },
		{ 0.020926, 0.035861, 0.082190, 0.320000 },
		{ 0.006437, 0.013100, 0.036390, 0.720000 },
		{ 0.000842, 0.002820, 0.019283, 1.280000 },
		{ 0.000051, 0.000185, 0.004717, 2.000000 } };

	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float snowMask = snowAlphaSpecTex.Sample(snowAlphaSpecSampler, screenPosition).y;
	float3 sourceColor = snowDiffuseTex.SampleLevel(snowDiffuseSampler, screenPosition, 0).xyz;

	if (1e-5 >= snowMask) {
		psout.Color = sourceColor;
		return psout;
	}

	float depth = depthTex.SampleLevel(depthSampler, screenPosition, 0).x;

	float3 sssColor = float3(0.784727991, 0.669085979, 0.560478985) * sourceColor;
	float2 texCoordStep = float2(0.078125, 0.13889) * g_SSSParameters.x / depth;
	float depthDiffFactor = 0.1 * g_SSSParameters.y;
	for (uint iterationIndex = 1; iterationIndex < 11; ++iterationIndex) {
		float2 iterationTexCoord = iterationParameters[iterationIndex].w * texCoordStep + input.TexCoord;
		float2 adjustedIterationTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(iterationTexCoord);
		float3 iterationSourceColor = snowDiffuseTex.SampleLevel(snowDiffuseSampler, adjustedIterationTexCoord, 0).xyz;
		float iterationDepth = depthTex.SampleLevel(depthSampler, adjustedIterationTexCoord, 0).x;
		float iterationDiffuseFactor = min(1, depthDiffFactor * abs(depth - iterationDepth));

		sssColor += iterationParameters[iterationIndex].xyz * lerp(iterationSourceColor, sourceColor, iterationDiffuseFactor);
	}
	float sssLuminance = Color::RGBToLuminanceAlternative(sssColor);
	float3 darkColor = max(0, lerp(g_SSSDarkColor.xyz * g_SSSDarkColor.w, sourceColor, sssLuminance));
	float3 snowColor = lerp(darkColor, sssColor, sssLuminance);

	psout.Color = lerp(sourceColor, snowColor, snowMask);

	return psout;
}
#endif
