// SSRT3 - Screen Space Ray Tracing 3
// Ported from SSRT3 by CDRIN (Olivier Therrien)
// https://github.com/cdrinmatern/SSRT3
//
// Based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion"
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
// Horizon-based sampling with 32-bit bitfield visibility tracking

#include "Common/FastMath.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/VR.hlsli"
#include "SSRT/common.hlsli"

///////////////////////////////////////////////////////////////////////////////

Texture2D<float> srcDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float3> srcRadiance : register(t2);

RWTexture2D<float4> outGIOcclusion : register(u0);

#define TILE_SIZE 8

// 32-bit bitmask for horizon tracking
#define MAX_RAY 32

///////////////////////////////////////////////////////////////////////////////
// Utility functions matching SSRT3's API with CS-specific implementations
///////////////////////////////////////////////////////////////////////////////

// Original uses _InverseProjectionMatrix; CS uses NDCToViewMul/Add
inline float3 PositionSSToVS(float2 uv, float depth)
{
	float linearDepth = SharedData::GetScreenDepth(depth);

	const float2 _mul = NDCToViewMul.xy;
	const float2 _add = NDCToViewAdd.xy;

	float3 posVS;
	posVS.xy = (_mul * uv + _add) * linearDepth;
	posVS.z = linearDepth;
	return posVS;
}

// SSRT3: GetNormalVS - reads view-space normal at screen pixel coordinate
// Original decodes from _NormalBufferTexture then transforms WS->VS;
// CS GBuffer already stores view-space normals
inline float3 GetNormalVS(uint2 positionSS)
{
	return GBuffer::DecodeNormal(srcNormalRoughness.Load(int3(positionSS, 0)).xy);
}

// SSRT3: GetNormalPyramidVS - reads view-space normal at UV with LOD
// Original decodes spheremap from _NormalPyramidTexture; CS has no normal pyramid,
// samples the flat normal buffer instead (lod unused)
inline float3 GetNormalPyramidVS(float2 uv, float lod)
{
	return GBuffer::DecodeNormal(srcNormalRoughness.SampleLevel(samplerPointClamp, uv, 0).xy);
}

///////////////////////////////////////////////////////////////////////////////
// Noise and math utilities (unchanged from SSRT3)
///////////////////////////////////////////////////////////////////////////////

// From Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
float SpatialOffsets(int2 position)
{
	return 0.25 * (float)((position.y - position.x) & 3);
}

// Interleaved gradient noise from Jimenez 2014 http://goo.gl/eomGso
float GradientNoise(float2 position)
{
	return frac(52.9829189 * frac(dot(position, float2(0.06711056, 0.00583715))));
}

// From http://byteblacksmith.com/improvements-to-the-canonical-one-liner-glsl-rand-for-opengl-es-2-0/
float rand(float2 co)
{
	float dt = dot(co.xy, float2(12.9898, 78.233));
	float sn = fmod(dt, 3.14);
	return frac(sin(sn) * 43758.5453);
}

// Fast acos approximation from GTAO
float2 GTAOFastAcos(float2 x)
{
	float2 outVal = -0.156583 * abs(x) + Math::HALF_PI;
	outVal *= sqrt(1.0 - abs(x));
	return x >= 0 ? outVal : Math::PI - outVal;
}

float GTAOFastAcos(float x)
{
	float outVal = -0.156583 * abs(x) + Math::HALF_PI;
	outVal *= sqrt(1.0 - abs(x));
	return x >= 0 ? outVal : Math::PI - outVal;
}

// RGB to HSV conversion (matching SSRT3's RgbToHsv)
float3 RgbToHsv(float3 c)
{
	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
	float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB conversion (matching SSRT3's HsvToRgb)
float3 HsvToRgb(float3 c)
{
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

///////////////////////////////////////////////////////////////////////////////

inline uint ComputeOccludedBitfield(float minHorizon, float maxHorizon, inout uint globalOccludedBitfield, out uint numOccludedZones)
{
	uint startHorizonInt = minHorizon * MAX_RAY;
	uint angleHorizonInt = ceil(saturate(maxHorizon - minHorizon) * MAX_RAY);
	uint angleHorizonBitfield = angleHorizonInt > 0 ? (0xFFFFFFFF >> ((32 - MAX_RAY) + (MAX_RAY - angleHorizonInt))) : 0;
	uint currentOccludedBitfield = angleHorizonBitfield << startHorizonInt;
	currentOccludedBitfield = currentOccludedBitfield & (~globalOccludedBitfield);
	globalOccludedBitfield = globalOccludedBitfield | currentOccludedBitfield;
	numOccludedZones = countbits(currentOccludedBitfield);
	return currentOccludedBitfield;
}

inline float3 HorizonSampling(
	bool directionIsRight, float3 posVS, float2 slideDir_TexelSize, float initialRayStep,
	float2 uv, float3 viewDir, float3 normalVS, float n, inout uint globalOccludedBitfield,
	float3 planeNormal)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	float stepRadius = ScreenSpaceSampling ?
	                       (Radius * (FrameDim.x / 2)) / (float)StepCount :
	                       max((Radius * HalfProjScale) / posVS.z, (float)StepCount);
	stepRadius /= ((float)StepCount + 1);
	float radiusVS = max(1, float(StepCount - 1)) * stepRadius;
	float samplingDirection = directionIsRight ? 1 : -1;
	float3 col = 0;
	float3 lastSamplePosVS = posVS;

	[loop] for (uint j = 0; j < StepCount; j++)
	{
		float offset = pow(abs((stepRadius * (j + initialRayStep)) / radiusVS), ExpFactor) * radiusVS;
		float2 uvOffset = slideDir_TexelSize * max(offset, 1 + j);
		float2 sampleUV = uv + uvOffset * samplingDirection;

		if (sampleUV.x <= 0 || sampleUV.y <= 0 || sampleUV.x >= 1 || sampleUV.y >= 1)
			break;

		int mipLevelOffset = MipOptimization ? min((j + 1) / 2, 4) : 0;
		
		float deviceDepth = srcDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevelOffset);

		if (deviceDepth == 1.0)
			continue;

		float3 samplePosVS = PositionSSToVS(sampleUV * frameScale, deviceDepth);

		float3 pixelToSample = normalize(samplePosVS - posVS);
		float linearThicknessMultiplier = LinearThickness ? saturate(samplePosVS.z / 100000.0) * 100 : 1;
		float3 pixelToSampleBackface = normalize((samplePosVS - (linearThicknessMultiplier * viewDir * Thickness)) - posVS);

		float2 frontBackHorizon = float2(dot(pixelToSample, viewDir), dot(pixelToSampleBackface, viewDir));
		frontBackHorizon = GTAOFastAcos(frontBackHorizon);
		frontBackHorizon = directionIsRight ? -frontBackHorizon.yx : frontBackHorizon.xy;
		// The math: https://www.desmos.com/calculator/je4y5ved2j
		// Using smoothstep for cos: https://discord.com/channels/586242553746030596/586245736413528082/1102228968247144570
		frontBackHorizon = smoothstep(0, 1, (frontBackHorizon + n) * (1.0 / Math::PI) + 0.5);

		uint numOccludedZones;
		ComputeOccludedBitfield(frontBackHorizon.x, frontBackHorizon.y, globalOccludedBitfield, numOccludedZones);

		if (numOccludedZones > 0) {
			float3 lightColor = srcRadiance.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevelOffset).rgb;
			float luminance = dot(lightColor, float3(0.2126, 0.7152, 0.0722));

			if (luminance > 0.001) {
				float3 lightDirectionVS = normalize(pixelToSample);
				float normalDotLightDirection = saturate(dot(normalVS, lightDirectionVS));

				if (normalDotLightDirection > 0.001) {
					float3 lightNormalVS;

#	ifdef NORMAL_APPROXIMATION
					lightNormalVS = -samplingDirection * cross(normalize(samplePosVS - lastSamplePosVS), planeNormal);
#	else
					lightNormalVS = GetNormalPyramidVS(sampleUV * frameScale, mipLevelOffset);
#	endif

					float lightNormalDotLightDirection = saturate(dot(lightNormalVS, -lightDirectionVS));

					col.xyz += (float(numOccludedZones) / float(MAX_RAY)) * lightColor * normalDotLightDirection * lightNormalDotLightDirection;
				}
			}
		}

		lastSamplePosVS = samplePosVS;
	}

	return col;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)] void main(const uint2 dtid : SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + .5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float deviceDepth = srcDepth.SampleLevel(samplerPointClamp, uv * frameScale, 0);

	if (deviceDepth == 1.0 ) {
		outGIOcclusion[pxCoord] = float4(0, 0, 0, 1);
		return;
	}

	float3 posVS = PositionSSToVS(uv * frameScale, deviceDepth);
	float3 normalVS = GetNormalVS(pxCoord);
	float3 viewDir = normalize(-posVS);

	float noiseOffset = SpatialOffsets(pxCoord);
	float noiseDirection = GradientNoise(pxCoord);
	float initialRayStep = frac(noiseOffset + TemporalOffsets) + (rand(uv) * 2.0 - 1.0) * 1.0 * float(JitterSamples);

	float ao = 0;
	float3 col = 0;

	[loop] for (uint i = 0; i < RotationCount; i++)
	{
		float rotationAngle = (i + noiseDirection + TemporalDirections) * (Math::PI / (float)RotationCount);
		float3 sliceDir = float3(cos(rotationAngle), sin(rotationAngle), 0);
		float2 slideDir_TexelSize = sliceDir.xy * RcpFrameDim;
		
		uint globalOccludedBitfield = 0;

		float3 planeNormal = normalize(cross(sliceDir, viewDir));
		float3 tangent = cross(viewDir, planeNormal);
		float3 projectedNormal = normalVS - planeNormal * dot(normalVS, planeNormal);
		float3 projectedNormalNormalized = normalize(projectedNormal);

		float cos_n = clamp(dot(projectedNormalNormalized, viewDir), -1, 1);
		float n = -sign(dot(projectedNormal, tangent)) * acos(cos_n);

		col += HorizonSampling(true, posVS, slideDir_TexelSize, initialRayStep,
			uv, viewDir, normalVS, n, globalOccludedBitfield, planeNormal);
		col += HorizonSampling(false, posVS, slideDir_TexelSize, initialRayStep,
			uv, viewDir, normalVS, n, globalOccludedBitfield, planeNormal);

		ao += float(countbits(globalOccludedBitfield)) / float(MAX_RAY);
	}

	// Finalize AO
	ao /= RotationCount;
	ao = saturate(pow(1.0 - saturate(ao), AOIntensity));

	// Finalize GI
	col /= RotationCount;
	col = col * GIIntensity;

	// HSV-space value clamping to prevent fireflies
	col = RgbToHsv(col);
	col.z = clamp(col.z, 0.0, 7);
	col = HsvToRgb(col);

	outGIOcclusion[pxCoord] = float4(col, ao);
}
