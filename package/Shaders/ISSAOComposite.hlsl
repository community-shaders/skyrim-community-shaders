#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState sourceSampler : register(s0);
SamplerState SAOSampler : register(s1);
SamplerState depthSampler : register(s2);
SamplerState NormalsSSRMaskSampler : register(s3);
SamplerState snowSpecAlphaSampler : register(s4);
SamplerState shadowMaskSampler : register(s5);
SamplerState SSRSourceSampler : register(s6);

Texture2D<float4> sourceTex : register(t0);
Texture2D<float4> SAOTex : register(t1);
Texture2D<float4> depthTex : register(t2);
Texture2D<float4> NormalsSSRMaskTex : register(t3);
Texture2D<float4> snowSpecAlphaTex : register(t4);
Texture2D<float4> shadowMaskTex : register(t5);
Texture2D<float4> SSRSourceTex : register(t6);

cbuffer PerGeometry : register(b2)
{
	float4 FogParam : packoffset(c0);
	float4 FogNearColor : packoffset(c1);
	float4 FogFarColor : packoffset(c2);
	float4 CameraNearFar : packoffset(c3);
	float4 LightDirection : packoffset(c4);
	float4 EyePosition : packoffset(c5);          // bEnableImprovedSnow in w
	float4 SparklesParameters1 : packoffset(c6);  // fSparklesIntensity in x, fSpecularSparklesIntensity in y, fSparklesSpecularPower in z, fSparklesDensity or fSparklesDensity - 0.5 if bUsePrecomputedNoise in w
	float4 SparklesParameters2 : packoffset(c7);  // fSparklesSize or fSparklesSize + 1 if bUsePrecomputedNoise in x, fNonSpecularSparklesIntensity in y, bToggleSparkles in z, bSparklesOnly in w
	float4 SparklesParameters3 : packoffset(c8);  // bDeactivateAOOnSnow in x, fSparklesMaxDistance in y, bUsePrecomputedNoise in z
	float4 SparklesParameters4 : packoffset(c9);  // fShadowSparkleIntensity in x, fSnowSparklesColorR in y, fSnowSparklesColorG in z, fSnowSparklesColorB in w
	float4 SSRParams : packoffset(c10);
};

float3 FastMod(float3 x, float divisor)
{
	return x - floor(x / divisor) * divisor;
}

float4 FastMod(float4 x, float divisor)
{
	return x - floor(x / divisor) * divisor;
}

float4 Permute(float4 x)
{
	return FastMod((x * 34.0 + 1.0) * x, 289);
}

float4 TaylorInvSqrt(float4 r)
{
	return 1.79284291400159 - 0.85373472095314 * r;
}

float SimplexNoise(float3 v)
{
	// First corner
	float3 i = floor(v + dot(v, 1.0 / 3.0));
	float3 x0 = v - i + dot(i, 1.0 / 6.0);

	// Other corners
	float3 g = step(x0.yzx, x0.xyz);
	float3 l = 1.0 - g;
	float3 i1 = min(g.xyz, l.zxy);
	float3 i2 = max(g.xyz, l.zxy);

	float3 x1 = x0 - i1 + 1.0 / 6.0;
	float3 x2 = x0 - i2 + 1.0 / 3.0;
	float3 x3 = x0 - 0.5;

	// Permutations
	i = FastMod(i, 289);
	float4 p = Permute(Permute(Permute(
								   i.z + float4(0.0, i1.z, i2.z, 1.0)) +
							   i.y + float4(0.0, i1.y, i2.y, 1.0)) +
					   i.x + float4(0.0, i1.x, i2.x, 1.0));

	// Gradients: 7x7 points over a square, mapped onto an octahedron.
	// The ring size 17*17 = 289 is close to a multiple of 49 (49*6 = 294)
	const float n = 7;

	float4 j = FastMod(p, n * n);

	float4 x_ = floor(j / n);
	float4 y_ = FastMod(j, n);

	float4 x = x_ * (2 / n) + (1 / (2 * n) - 1);
	float4 y = y_ * (2 / n) + (1 / (2 * n) - 1);
	float4 h = 1.0 - abs(x) - abs(y);

	float4 b0 = float4(x.xy, y.xy);
	float4 b1 = float4(x.zw, y.zw);

	float4 s0 = floor(b0) * 2.0 + 1.0;
	float4 s1 = floor(b1) * 2.0 + 1.0;
	float4 sh = -step(h, 0);

	float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
	float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

	float3 p0 = float3(a0.xy, h.x);
	float3 p1 = float3(a0.zw, h.y);
	float3 p2 = float3(a1.xy, h.z);
	float3 p3 = float3(a1.zw, h.w);

	// Normalise gradients
	float4 norm = TaylorInvSqrt(float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
	p0 *= norm.x;
	p1 *= norm.y;
	p2 *= norm.z;
	p3 *= norm.w;

	// Mix final noise value
	float4 m = max(0.6 - float4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
	return 42.0 * dot(pow(m, 4), float4(dot(p0, x0), dot(p1, x1),
									 dot(p2, x2), dot(p3, x3)));
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float ao = SAOTex.Sample(SAOSampler, screenPosition).x;
	float4 sourceColor = sourceTex.SampleLevel(sourceSampler, screenPosition, 0);

	float4 composedColor = sourceColor;

	if (0.5 < SSRParams.z) {
		float2 ssrMask = NormalsSSRMaskTex.SampleLevel(NormalsSSRMaskSampler, screenPosition, 0).zw;
		float4 ssr = SSRSourceTex.Sample(SSRSourceSampler, screenPosition);

		float3 ssrInput = 0;
		if (1e-5 >= ssrMask.x && 1e-5 < ssrMask.y) {
			ssrInput = min(SSRParams.y * sourceColor.xyz, max(0, SSRParams.x * (ssr.xyz * ssr.w)));
		}
		composedColor.xyz += ssrInput;
	}

	float snowMask = 0;
#	if !defined(VR)
	if (EyePosition.w != 0) {
		float2 specSnow = snowSpecAlphaTex.Sample(snowSpecAlphaSampler, screenPosition).xy;
		composedColor.xyz += specSnow.x * specSnow.y;
		snowMask = specSnow.y;
	}
#	endif

#	if defined(APPLY_SAO)
	if (EyePosition.w != 0 && 1e-5 < snowMask) {
		ao = min(1, SparklesParameters3.x + ao);
	}
	composedColor.xyz = Color::GammaToLinear(composedColor.xyz);
	composedColor.xyz *= pow(ao, 1.5);
	composedColor.xyz = Color::LinearToGamma(composedColor.xyz);
#	endif

	float depth = depthTex.SampleLevel(depthSampler, screenPosition, 0).x;

#	if defined(APPLY_FOG)
	float fogDistanceFactor = (2 * CameraNearFar.x * CameraNearFar.y) / ((CameraNearFar.y + CameraNearFar.x) - (2 * (1.01 * depth - 0.01) - 1) * (CameraNearFar.y - CameraNearFar.x));
	float fogFactor = min(FogParam.w, pow(saturate(fogDistanceFactor * FogParam.y - FogParam.x), FogParam.z));
	float3 fogColor = lerp(FogNearColor.xyz, FogFarColor.xyz, fogFactor);
	if (depth < 0.999999) {
		composedColor.xyz = FogNearColor.w * lerp(composedColor.xyz, fogColor, fogFactor);
	}
#	endif

#	if !defined(VR)
	float sparklesInput = 0;
	if (EyePosition.w != 0 && snowMask != 0 && 1e-5 < SparklesParameters2.z) {
		float shadowMask = shadowMaskTex.SampleLevel(shadowMaskSampler, screenPosition, 0).x;

		float4 vsPosition = float4(2 * input.TexCoord.x - 1, 1 - 2 * input.TexCoord.y, depth, 1);

		float4 csPosition = mul(FrameBuffer::CameraViewProjInverse[0], vsPosition);
		csPosition.xyz /= csPosition.w;
		csPosition.xyz += FrameBuffer::CameraPosAdjust[0].xyz;

		float3 noiseSeed = 0.07 * (SparklesParameters2.x * csPosition.xyz);
		float noiseValue = 0.5 * (SimplexNoise(noiseSeed) + 1);
		float sparklesSpecular = pow(abs(noiseValue * saturate(rcp(noiseValue))), SparklesParameters1.z);

		float sparklesAttenuation = 1 - smoothstep(0, 1, length(csPosition.xyz - EyePosition.xyz) / SparklesParameters3.y);

		float sparklesShadowing = lerp(shadowMask, 1, SparklesParameters4.x);

		sparklesInput = SparklesParameters1.x * sparklesSpecular * step(SparklesParameters1.w, sparklesSpecular);
		sparklesInput *= sparklesShadowing;
		sparklesInput *= sparklesAttenuation;
	}
	sparklesInput *= snowMask;
	float4 sparklesColor = float4(SparklesParameters4.yzw * sparklesInput, sparklesInput);

	composedColor *= 1 - SparklesParameters2.w;
	composedColor += sparklesColor;
#	endif

	psout.Color = composedColor;

	return psout;
}
#endif
