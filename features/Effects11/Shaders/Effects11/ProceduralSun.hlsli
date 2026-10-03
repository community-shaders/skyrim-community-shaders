#ifndef EFFECTS11_PROCEDURAL_SUN_HLSLI
#define EFFECTS11_PROCEDURAL_SUN_HLSLI

// Physical procedural sun disc for the Effects 11 [PROCEDURALSUN] path.
//
// GetHestrofferLimbDarkening is adapted from Physical Sky:
// MIT License
//
// Copyright (c) 2024 Pentalimbed/FiveLimbedCat/ProfJack
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

namespace ProceduralSun
{
	// Hestroffer limb darkening profile (license above).
	// http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
	float3 GetHestrofferLimbDarkening(float normalizedRadius)
	{
		float mu = sqrt(saturate(1.0f - normalizedRadius * normalizedRadius));

		float3 a0 = float3(0.34685f, 0.26073f, 0.15248f);
		float3 a1 = float3(1.37539f, 1.27428f, 1.38517f);
		float3 a2 = float3(-2.04425f, -1.30352f, -1.49615f);
		float3 a3 = float3(2.70493f, 1.47085f, 1.99886f);
		float3 a4 = float3(-1.94290f, -0.96618f, -1.48155f);
		float3 a5 = float3(0.55999f, 0.26384f, 0.44119f);

		float mu2 = mu * mu;
		float mu3 = mu2 * mu;
		float mu4 = mu2 * mu2;
		float mu5 = mu4 * mu;
		return max(a0 + a1 * mu + a2 * mu2 + a3 * mu3 + a4 * mu4 + a5 * mu5, 0.0f);
	}

	float GetInfluenceCos(float sunDiskCos, bool haloEnabled, float sunHaloCos, float haloIntensity)
	{
		bool haloActive = haloEnabled && haloIntensity > 0.0f && sunHaloCos < sunDiskCos;
		return haloActive ? sunHaloCos : sunDiskCos;
	}

	void EvaluateDisc(float cosTheta, float sunDiskCos, float edgeSoftness, out float3 limbDarkening, out float coverage)
	{
		limbDarkening = 0.0f;
		coverage = 0.0f;
		if (cosTheta <= sunDiskCos || sunDiskCos <= 0.0f || sunDiskCos >= 1.0f)
			return;

		float sunDiskSin = sqrt(max(1.0f - sunDiskCos * sunDiskCos, 1e-8f));
		float tanTheta = sqrt(saturate(1.0f - cosTheta * cosTheta)) / max(cosTheta, 1e-5f);
		float normalizedRadius = saturate(tanTheta * sunDiskCos / sunDiskSin);
		float edgeWidth = max((1.0f - sunDiskCos) * edgeSoftness, 1e-8f);

		limbDarkening = GetHestrofferLimbDarkening(normalizedRadius);
		coverage = saturate((cosTheta - sunDiskCos) / edgeWidth);
	}

	// The Effects 11 rational corona profile, in angular cosine space.
	float EvaluateHalo(float cosTheta, float sunDiskCos, float sunHaloCos, float haloFalloff)
	{
		if (sunHaloCos >= sunDiskCos || cosTheta <= sunHaloCos)
			return 0.0f;

		float normalizedDistance = saturate((sunDiskCos - cosTheta) / max(sunDiskCos - sunHaloCos, 1e-8f));
		return (1.0f - normalizedDistance) * rcp(1.0f + max(haloFalloff, 0.0f) * normalizedDistance);
	}

	void ComposeDiscAndHalo(
		float3 limbDarkening,
		float discCoverage,
		float diskIntensity,
		float haloProfile,
		float haloIntensity,
		out float3 sunColor,
		out float sunCoverage)
	{
		haloProfile = haloIntensity > 0.0f ? saturate(haloProfile) : 0.0f;
		sunCoverage = max(saturate(discCoverage), haloProfile);
		float3 premultipliedSun = limbDarkening * diskIntensity * discCoverage + haloIntensity * haloProfile;
		sunColor = premultipliedSun / max(sunCoverage, 1e-5f);
	}

	float4 ToAdditiveBlend(float4 color, float radianceLimit)
	{
		float3 emitted = max(color.xyz, 0.0f) * saturate(color.w);
		float peak = max(emitted.x, max(emitted.y, emitted.z));
		if (peak <= 0.0f)
			return 0.0f;

		float alpha = clamp(peak / max(radianceLimit, 1.0f), 1.0f / 256.0f, 1.0f);
		return float4(emitted / alpha, alpha);
	}

	float GetCloudTransmission(float capturedCloudOcclusion, float opticalDepthScale)
	{
		float cloudOpacity = saturate(capturedCloudOcclusion);
		if (opticalDepthScale <= 0.0f || cloudOpacity <= 0.0f)
			return 1.0f;
		return pow(saturate(1.0f - cloudOpacity), opticalDepthScale);
	}

	float GetGlareCloudTransmission(float capturedCloudOcclusion, float extinction)
	{
		if (extinction <= 0.0f)
			return 1.0f;
		return GetCloudTransmission(capturedCloudOcclusion, 1.0f + 0.5f * extinction);
	}
}

#endif
