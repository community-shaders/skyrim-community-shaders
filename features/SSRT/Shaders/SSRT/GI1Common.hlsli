// AMD Capsaicin GI-1.1 Screen-Space Probes — Common definitions
// Ported for Community Shaders DX11 (no raytracing)

#ifndef GI1_COMMON_HLSLI
#define GI1_COMMON_HLSLI

#include "Common/GBuffer.hlsli"
#include "Common/SharedData.hlsli"

// Invalid ID sentinel
#define kGI1_InvalidId 0xFFFFFFFFu

// Quantization factor for atomic radiance updates
#define kGI1_FloatQuantize 1e4f

// Angular threshold for probe radiance reuse
#define kGI1_AngleThreshold cos(2e-2f * 3.1415926535897932f)

// Max hit distance sentinel
#define MAX_HIT_DISTANCE 1e9f

// Math constants
#ifndef PI
#	define PI 3.1415926535897932f
#endif
#ifndef HALF_PI
#	define HALF_PI 1.5707963267948966f
#endif
#ifndef QUARTER_PI
#	define QUARTER_PI 0.78539816339744831f
#endif
#ifndef TWO_PI
#	define TWO_PI 6.283185307179586f
#endif
#ifndef INV_PI
#	define INV_PI 0.31830988618379067f
#endif
#ifndef INV_TWO_PI
#	define INV_TWO_PI 0.15915494309189534f
#endif
#ifndef TWO_INV_PI
#	define TWO_INV_PI 0.63661977236758134f
#endif
#ifndef FLT_MAX
#	define FLT_MAX 3.402823466e+38f
#endif
#ifndef FLT_EPSILON
#	define FLT_EPSILON 1.192092896e-07f
#endif

// Feature constant buffer at b1
cbuffer SSRTCB : register(b1)
{
	row_major float4x4 g_Reprojection;
	row_major float4x4 g_PrevViewProjInverse;

	float3 g_Eye;
	uint g_FrameIndex;

	float2 g_BufferDimensions;
	float2 g_RcpBufferDimensions;

	float2 g_NearFar;
	float g_CellSize;
	uint g_ProbeSize;

	uint g_ProbeCountX;
	uint g_ProbeCountY;
	uint g_ProbeMaskMipCount;
	uint g_ProbeSpawnTileSize;

	int g_BlurDirectionX;
	int g_BlurDirectionY;
	uint g_MaxHiZSteps;
	float g_HiZThickness;

	float g_HiZMaxDistance;
	float g_GIIntensity;
	float g_AOIntensity;
	uint g_MaxSpawnCount;

	uint g_MaxRayCount;
	uint g_DepthPyramidMipCount;
	float g_pad1;
	float g_pad2;
};

// Convenience accessors matching Capsaicin naming
#define g_BlurDirection int2(g_BlurDirectionX, g_BlurDirectionY)

// Samplers: s0 = point clamp, s1 = linear clamp
SamplerState g_PointClampSampler : register(s0);
SamplerState g_LinearClampSampler : register(s1);

// Luminance (BT.709)
float luminance(float3 color)
{
	return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// squared
float squared(float x)
{
	return x * x;
}

// World position from UV + depth (matches DeferredCompositeCS pattern)
float3 reconstructWorldPosition(float2 uv, float depth)
{
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[0], positionWS);
	return positionWS.xyz / positionWS.w;
}

// World position from UV + depth via previous frame ViewProjInverse
float3 reconstructPrevWorldPosition(float2 uv, float depth)
{
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(g_PrevViewProjInverse, positionWS);
	return positionWS.xyz / positionWS.w;
}

// Linearize depth using near/far (reverse-Z convention, matching Capsaicin)
float toLinearDepth(float depth, float2 nearFar)
{
	return (nearFar.x * nearFar.y) / (nearFar.x + depth * (nearFar.y - nearFar.x));
}

#endif  // GI1_COMMON_HLSLI
