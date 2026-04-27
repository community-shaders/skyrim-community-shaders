#define NUM_SAMPLES 16

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float> srcNDCDepth : register(t0);
RWTexture2D<unorm float> shadowOutput : register(u0);

SamplerState samplerPointClamp : register(s1);

cbuffer SSSCB : register(b1)
{
	float2 FrameDim;
	float2 RcpTexDim;

	float2 TexDim;
	float2 DynamicRes;

	float SurfaceThickness;
	float ShadowContrast;
	float RayLength;
	uint CurrentMip;

	float3 LightWorldDir;
	uint pad;
};

// Convert texture UV [0, DynamicRes] to per-eye NDC [-1, 1].
float2 TexUVToNDC(float2 texUV, uint eyeIdx)
{
#ifdef VR
	float2 eyeScreenUV = float2(
		(texUV.x - float(eyeIdx) * DynamicRes.x * 0.5) / (DynamicRes.x * 0.5),
		texUV.y / DynamicRes.y);
#else
	float2 eyeScreenUV = texUV / DynamicRes;
#endif
	return eyeScreenUV * float2(2.0, -2.0) + float2(-1.0, 1.0);
}

// Reconstruct view-space position from texture UV [0, DynamicRes] and NDC depth.
float3 ReconstructViewPos(float2 texUV, float ndcDepth, uint eyeIdx)
{
	float2 ndc = TexUVToNDC(texUV, eyeIdx);
	float4 viewPos = mul(FrameBuffer::CameraProjInverse[eyeIdx], float4(ndc, ndcDepth, 1.0));
	return viewPos.xyz / viewPos.w;
}

// Project a view-space position into texture UV [0, DynamicRes].
float2 ViewPosToTexUV(float3 posVS, uint eyeIdx)
{
	float2 screenUV = FrameBuffer::ViewToUV(posVS, true, eyeIdx);  // per-eye [0, 1]
#ifdef VR
	return float2(
		float(eyeIdx) * DynamicRes.x * 0.5 + screenUV.x * DynamicRes.x * 0.5,
		screenUV.y * DynamicRes.y);
#else
	return screenUV * DynamicRes;
#endif
}

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	uint mipScale = 1u << CurrentMip;
	uint2 effectiveFrameDim = uint2(FrameDim) >> CurrentMip;

#if defined(VR) && defined(RIGHT)
	static const uint eyeIndex = 1;
	uint2 actualDtid = uint2(dtid.x + effectiveFrameDim.x / 2u, dtid.y);
	if (actualDtid.x >= effectiveFrameDim.x || actualDtid.y >= effectiveFrameDim.y)
		return;
#elif defined(VR)
	static const uint eyeIndex = 0;
	uint2 actualDtid = dtid;
	if (dtid.x >= effectiveFrameDim.x / 2u || dtid.y >= effectiveFrameDim.y)
		return;
#else
	static const uint eyeIndex = 0;
	uint2 actualDtid = dtid;
	if (any(float2(dtid) >= float2(effectiveFrameDim)))
		return;
#endif

	// UV in [0, DynamicRes] space — directly usable as SampleLevel UV for the mip-N depth
	// texture (whose active region spans [0, DynamicRes]).  Also feeds ReconstructViewPos.
	float2 startUV = (float2(actualDtid) + 0.5) * RcpTexDim * float(mipScale);
	float startNDC = srcNDCDepth.SampleLevel(samplerPointClamp, startUV, 0);

	float occ = 0;

	// Valid surface NDC is strictly between 0 and 1 — works for both standard and reversed Z.
	if (startNDC > 0.0 && startNDC < 1.0) {
		float3 startPosVS = ReconstructViewPos(startUV, startNDC, eyeIndex);

		// March toward the sun in view space.
		float3 lightViewDir = normalize(mul((float3x3)FrameBuffer::CameraView[eyeIndex], LightWorldDir));
		float3 endPosVS = startPosVS + lightViewDir * RayLength;
		float2 endUV = ViewPosToTexUV(endPosVS, eyeIndex);

		uint startIndex;
		uint endIndex;
		
		if (CurrentMip == 1)      { startIndex = 1;  endIndex = 2;  }
		else if (CurrentMip == 2) { startIndex = 3;  endIndex = 6;  }
		else if (CurrentMip == 3) { startIndex = 7;  endIndex = 14; }
		else                    { startIndex = 15; endIndex = 30; }

		for (uint i = startIndex; i <= endIndex; i++)
		{
			float t = float(i) / float(32);
			t = 1.0 - t;
			float2 sampleUV = lerp(startUV, endUV, t);
			float3 rayPosVS = lerp(startPosVS, endPosVS, t);

			float sampleNDC = srcNDCDepth.SampleLevel(samplerPointClamp, sampleUV, 0);
			if (sampleNDC > 0.0 && sampleNDC < 1.0) {
				float surfaceZ = SharedData::GetScreenDepth(sampleNDC);
				float depth_delta = rayPosVS.z - surfaceZ;

				if ((depth_delta > 0.0f) && (depth_delta < (SurfaceThickness))){
					occ = max(1, occ);
				}
			}
		}
	}

	shadowOutput[actualDtid] = 1.0 - occ;
}
