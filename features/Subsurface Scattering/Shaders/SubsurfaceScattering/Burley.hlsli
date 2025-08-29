#include "Common/GBuffer.hlsli"
#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"
#include "Common/Math.hlsli"

// [Per H. Christensen, Brent Burley 2015, "Approximate Reflectance Profiles for Efficient Subsurface Scattering"]
// https://graphics.pixar.com/library/ApproxBSSRDF/paper.pdf
float3 GetBurleyCDF(float3 d, float3 r, float rand)
{
	return 1 - 0.25 * exp(-r / d) - 0.75 * exp(-r / (3 * d)) - rand;
}

float GetBurleyPDF(float r, float l, float s)
{
	float d = l / s;
	float pdf = 0.25 / d * (exp(-r / d) + exp(-r / (3 * d))); // cdf dr
	return max(pdf, 1e-5f);
}

// [Tiantian Xie et al. 2020, "Real-time subsurface scattering with single pass variance-guided adaptive importance sampling"]
// https://thisistian.github.io/publication/spvg_xie_2020_I3D_small.pdf
// Also check https://zero-radiance.github.io/post/sampling-diffusion/
float RadiusApprox(float d, float rand)
{
	// g(ξ) = d((2 − c)ξ − 2)log(1 − ξ)
	// minimal mean squared error when c = 2.5715
	return d * ((2 - 2.5715f) * rand - 2) * log(1 - rand);
}

float3 GetBurleyProfile(float3 l, float3 s, float radius)
{
	// R(d,r) = \frac{e^{-r/d}+e^{-r/(3d)}}{8*pi*d*r}
	float3 d = 1.f / s;
	float3 r = radius / l;
	float3 negRbyD = -r / d;
	return max((exp(negRbyD) + exp(negRbyD / 3.0f)) / (d * l * 8 * Math::PI), 1e-12f);
}

float3 GetScalingFactor(float3 albedo)
{
	// we have three methods for calculating the scaling factor
	// d = l / (1.85 − A + 7|A − 0.8|^3)
	// d = l / (1.9 − A + 3.5(A − 0.8)^2)
	// d = l / (3.5 + 100(A − 0.33)^4)
	// here we choose the third to use diffuse mean free path as parameter.
	float3 value = albedo - 0.33f;
	return 3.5f + 100.f * pow(abs(value), 4);
}

float4 BurleyNormalizedSS(uint2 DTid, float2 texCoord, uint eyeIndex, float sssAmount, bool humanProfile)
{
	float centerDepth = SharedData::GetScreenDepth(DepthTexture[DTid].x);

	float4 centerColor = ColorTexture[DTid];
	if (sssAmount == 0 || centerDepth <= 0)
	{
		return centerColor;
	}

	float4 surfaceAlbedo = AlbedoTexture[DTid];
	float3 originalColor = Color::IrradianceToLinear(centerColor.xyz / max(surfaceAlbedo.xyz, EPSILON_SSS_ALBEDO));

	float4 diffuseMeanFreePath = humanProfile ? MeanFreePathHuman : MeanFreePathBase;
	diffuseMeanFreePath.xyz = float3(max(diffuseMeanFreePath.x, 1e-5f), max(diffuseMeanFreePath.y, 1e-5f), max(diffuseMeanFreePath.z, 1e-5f));
	diffuseMeanFreePath *= sssAmount;

	float dmfpForSampling = diffuseMeanFreePath.w;
	float s = GetScalingFactor(surfaceAlbedo.www).x;
	float d = dmfpForSampling / s;
	float3 s3d = GetScalingFactor(surfaceAlbedo.xyz);
	float3 d3d = diffuseMeanFreePath.xyz * dmfpForSampling / s3d;

	const float3 normalVS = GBuffer::DecodeNormal(NormalTexture[DTid].xy);
	const float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

	float3 weightSum = 0.0f;
	float3 colorSum = 0.0f;

	float2 uvScale = (GAME_UNIT_TO_CM * 0.1f * (0.5f / tan(0.5 * radians(SSSS_FOVY)))) / centerDepth;  // Scale in mm

	// center sample weight
	float centerRadius = 0.5f * (SharedData::BufferDim.z / uvScale.x + SharedData::BufferDim.w / uvScale.y);
	float centerRadiusCDF = GetBurleyCDF(d, centerRadius, 0).x;
	float3 centerWeight = GetBurleyCDF(d3d, centerRadius, 0);

	int3 seed = int3(DTid.xy, 0);
	int seedStart = Random::pcg3d(int3(seed.xy, SharedData::FrameCount)).x;

	[loop]
	for (int i = 0; i < BurleySamples; ++i)
	{
		seed.z = seedStart++;
		float2 rand = float2(Random::pcg3d(seed).xy) / 4294967296.0f;  // to [0, 1)

		rand.x = centerRadiusCDF + rand.x * (1.0f - centerRadiusCDF);

		// generate radius & angle for sampling
		float radius = max(RadiusApprox(d, rand.x), 1e-5f);
		float theta = rand.y * 2.0f * Math::PI;

		float pdf = GetBurleyPDF(radius, dmfpForSampling, s);

		float2 uvOffset = uvScale * radius;
		uvOffset.x *= cos(theta);
		uvOffset.y *= sin(theta);

		float2 sampleUV = texCoord + uvOffset;
		float2 clampedUV = clamp(sampleUV, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
		uint2 samplePixcoord = uint2(clampedUV * SharedData::BufferDim.xy);
		float3 sampleColor = Color::IrradianceToLinear(ColorTexture[samplePixcoord].xyz / max(AlbedoTexture[samplePixcoord].xyz, EPSILON_SSS_ALBEDO));
		float sampleDepth = SharedData::GetScreenDepth(DepthTexture[samplePixcoord].x);
		float3 sampleNormalVS = GBuffer::DecodeNormal(NormalTexture[samplePixcoord].xy);
		float3 sampleNormalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(sampleNormalVS, 0)).xyz);

		float deltaDepth = (sampleDepth - centerDepth) * 10.f / GAME_UNIT_TO_CM;  // convert to mm
		float radiusSampledInMM = sqrt(radius * radius + deltaDepth * deltaDepth);

		float maskSample = MaskTexture[samplePixcoord].x;
		bool mask = maskSample > 1e-5f;

		float3 diffusionProfile = GetBurleyProfile(diffuseMeanFreePath.xyz, s3d, radiusSampledInMM);
		float normalWeight = sqrt(saturate(dot(sampleNormalWS, normalWS) * 0.5f + 0.5f));
		float3 sampleWeight = mask ? (diffusionProfile / pdf) * normalWeight : 0.0f;

		colorSum += sampleWeight * sampleColor;
		weightSum += sampleWeight;
	}

	colorSum *= any(weightSum == 0.0f) ? 0.0f : (1.0f / weightSum);
	colorSum = lerp(colorSum, originalColor, saturate(centerWeight));
	float3 color = Color::IrradianceToGamma(colorSum) * AlbedoTexture[DTid.xy].xyz;

	float4 outColor = float4(color, ColorTexture[DTid.xy].w);
	return outColor;
}