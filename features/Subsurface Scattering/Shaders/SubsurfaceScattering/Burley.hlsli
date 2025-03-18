#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"
#include "Common/Math.hlsli"

#define BURLEY_NUM_SAMPLES	64
#define BURLEY_INV_NUM_SAMPLES (1.0f/BURLEY_NUM_SAMPLES)

#define EXPONENTIAL_WEIGHT 0.2f

// Threshold value at which model switches from SSS to default lit
#define SSSS_OPACITY_THRESHOLD_EPS 0.10

// Burley constants
#define BURLEY_MM_2_CM		0.1f
#define BURLEY_CM_2_MM      10.0f

// Required if we use a texture format with limited size but want to express a larger radius
#define SUBSURFACE_RADIUS_SCALE 1024;

 // The kernels range from -3 to 3
#define SUBSURFACE_KERNEL_SIZE 3;

#define CLAMP_PDF 0.00001

struct FBurleyParameter
{
	float4 SurfaceAlbedo;
	float4 DiffuseMeanFreePath;
	float  WorldUnitScale;
	float  SurfaceOpacity; // 1.0 means full Burley, 0.0 means full Default Lit
};

struct FBurleySampleInfo
{
	float RadiusInMM;
	float Theta;
	float Pdf;
	float CosTheta;
	float SinTheta;
};

struct BurleySampleDiffuseNormal
{
	float4 DiffuseLighting;
	float3 WorldNormal;
};

float GetDiffuseMeanFreePathForSampling(float4 DiffuseMeanFreePath)
{
	return DiffuseMeanFreePath.a;
}

float GetComponentForScalingFactorEstimation(float4 SurfaceAlbedo)
{
	return SurfaceAlbedo.a;
}

float3 GetSubsurfaceProfileBoundaryColorBleed(uint SubsurfaceProfileInt)
{
    return float3(1.f, 1.f, 1.f);
}

float3 GetDiffuseReflectProfileWithDiffuseMeanFreePath(float3 L, float3 S3D, float Radius)
{
	//rR(r)
	float3 D = 1 / S3D;
	float3 R = Radius / L;
	const float Inv8Pi = 1.0 / (8 * Math::PI);
	float3 NegRbyD = -R / D;
	return max((exp(NegRbyD) + exp(NegRbyD / 3.0)) / (D * L) * Inv8Pi, 0.000000000001f /*Fix color shift due to precision issue for substrate per-pixel MFP SSS*/);
}

float GetSearchLightDiffuseScalingFactor(float SurfaceAlbedo)
{
	float Value = SurfaceAlbedo - 0.33;
	return 3.5 + 100 * Value * Value * Value * Value;
}

float3 GetSearchLightDiffuseScalingFactor3D(float3 SurfaceAlbedo)
{
	float3 Value = SurfaceAlbedo - 0.33;
	return 3.5 + 100 * Value * Value * Value * Value;
}

float GetScalingFactor(float A)
{

	float S = GetSearchLightDiffuseScalingFactor(A);
	return S;
}

float3 GetScalingFactor3D(float3 SurfaceAlbedo)
{
	float3 S3D = GetSearchLightDiffuseScalingFactor3D(SurfaceAlbedo);
	return S3D;
}

uint3 Rand3DPCG16(int3 p)
{
	// taking a signed int then reinterpreting as unsigned gives good behavior for negatives
	uint3 v = uint3(p);

	// Linear congruential step. These LCG constants are from Numerical Recipies
	// For additional #'s, PCG would do multiple LCG steps and scramble each on output
	// So v here is the RNG state
	v = v * 1664525u + 1013904223u;

	// PCG uses xorshift for the final shuffle, but it is expensive (and cheap
	// versions of xorshift have visible artifacts). Instead, use simple MAD Feistel steps
	//
	// Feistel ciphers divide the state into separate parts (usually by bits)
	// then apply a series of permutation steps one part at a time. The permutations
	// use a reversible operation (usually ^) to part being updated with the result of
	// a permutation function on the other parts and the key.
	//
	// In this case, I'm using v.x, v.y and v.z as the parts, using + instead of ^ for
	// the combination function, and just multiplying the other two parts (no key) for 
	// the permutation function.
	//
	// That gives a simple mad per round.
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;

	// only top 16 bits are well shuffled
	return v >> 16u;
}

float2 Generate2DRandomNumber(int3 Seed)
{
	// return Random::R2Sequence(Seed.z);
	return float2(Rand3DPCG16(Seed).xy) / 0x10000;
}

float GetCDF(float D, float X, float XI)
{
	return 1 - 0.25*exp(-X / D) - 0.75*exp(-X / (3 * D)) - XI;
}
float GetCDFDeriv1(float D, float X)
{
	return 0.25 / D * (exp(-X / D) + exp(-X / (3 * D)));
}
float GetCDFDeriv1InversD(float InvD, float X)
{
	return 0.25 * InvD * (exp(-X*InvD)+exp(-3*X*InvD));
}

float GetCDFDeriv2(float D, float X)
{
	return exp(-X / D)*(-0.0833333*exp(2 * X / (3 * D)) - 0.25) / (D*D);
}


BurleySampleDiffuseNormal SampleSSSColorConsideringLocalShared(float2 CenterUV, float2 UVOffset, uint2 CenterGroupThreadID, float MipLevel)
{
	// Fix border flickering when mipmaps got garbage data.
	float2 ClampedUV = clamp(CenterUV + UVOffset, 0, 1);

	BurleySampleDiffuseNormal Sample;
	uint2 DTid = uint2(ClampedUV / SharedData::BufferDim.zw - 0.5f);
	Sample.DiffuseLighting = ColorTexture[DTid];
	Sample.WorldNormal = NormalTexture[DTid].xyz;

	return Sample;
}

float2 CalculateBurleyScale(float WorldUnitScale, float DepthAtCenter)
{
	float2 BurleyScale = WorldUnitScale;

	float distanceToProjectionWindow = 1.0 / tan(0.5 * radians(SSSS_FOVY));
	BurleyScale *= distanceToProjectionWindow / DepthAtCenter;
	
	// cast from cm to mm for depth, and remove the effect of SUBSURFACE_KERNEL_SIZE. 
	BurleyScale *= 1 / BURLEY_CM_2_MM;

	// account for Screen Percentage/Dyanmic Resolution Scaling
	// BurleyScale *= (SubsurfaceInput0_ViewportSize.x * SubsurfaceInput0_ExtentInverse.x);
	// BurleyScale.y *= (SubsurfaceInput0_Extent.x * SubsurfaceInput0_ExtentInverse.y);

	return BurleyScale;
}

// Brian's approximation.
float RadiusRootFindByApproximation(float D, float RandomNumber)
{
	return D * ((2 - 2.6)*RandomNumber - 2)*log(1 - RandomNumber);
}

// Get the probability to sample a disk.
float GetPdf(float Radius, float L, float S)
{
	//without clamp, the result will be zero which will lead to infinity for R/pdf. and Nan for the weighted sum.
	//we need to clamp this to a very small pdf.

	float Pdf = GetCDFDeriv1(L / S, Radius);
	return  max(Pdf, CLAMP_PDF);
}

// Given the Depth and the BurleyParameter, figure out the actual radius of the center pixel in MM,
// taking into account the depth and screen dimensions.
float CalculateCenterSampleRadiusInMM(FBurleyParameter BurleyParameter, float Depth, float2 texCoord)
{
	float DiffuseMeanFreePath = GetDiffuseMeanFreePathForSampling(BurleyParameter.DiffuseMeanFreePath);

	float A = GetComponentForScalingFactorEstimation(BurleyParameter.SurfaceAlbedo);
	float S = GetScalingFactor(A);
	float3 S3D = GetScalingFactor3D(BurleyParameter.SurfaceAlbedo.xyz);

	float2 BurleyScale = CalculateBurleyScale(BurleyParameter.WorldUnitScale,Depth);

	// In the reference function, UVOffset = BurleyScale * RadiusInMM
	//      float2 UVOffset = BurleyScale*BurleySampleInfo.RadiusInMM;
	// So, given the UV offset, we can find the distance in mm as:
	//      float DistInMM = UvOffset.x/BurleyScale.x + UvOffset.y/BurleyScale.y;
	// But for stability, we can just average them.
	float CenterSampleRadiusInMM = 0.5f * (texCoord.x/BurleyScale.x + texCoord.y/BurleyScale.y);

	return CenterSampleRadiusInMM;
}

// Given the UV and BurleyParameter, determine how much RGB weight should be assigned to the center 
// pixel. The rest of the weight would be applied from the blur.
float3 CalculateCenterSampleWeight(float Depth, FBurleyParameter BurleyParameter, float2 texCoord)
{
	float CenterSampleRadiusInMM = CalculateCenterSampleRadiusInMM(BurleyParameter, Depth, texCoord);

	float DiffuseMeanFreePath = GetDiffuseMeanFreePathForSampling(BurleyParameter.DiffuseMeanFreePath);

	// To calculate the surface free path from albedo, use the default scaling.
	float3 D = DiffuseMeanFreePath / GetScalingFactor3D(BurleyParameter.SurfaceAlbedo.xyz);

	float3 CenterSampleWeight;

	CenterSampleWeight.x = GetCDF(D.x,CenterSampleRadiusInMM,0);
	CenterSampleWeight.y = GetCDF(D.y,CenterSampleRadiusInMM,0);
	CenterSampleWeight.z = GetCDF(D.z,CenterSampleRadiusInMM,0);

	return CenterSampleWeight;
}

float CalculateCenterSampleCdf(FBurleyParameter BurleyParameter, float Depth, float2 texCoord)
{
	float CenterSampleRadiusInMM = CalculateCenterSampleRadiusInMM(BurleyParameter, Depth, texCoord);

	float DiffuseMeanFreePathForSampling = GetDiffuseMeanFreePathForSampling(BurleyParameter.DiffuseMeanFreePath);
	float A = GetComponentForScalingFactorEstimation(BurleyParameter.SurfaceAlbedo);
	float S = GetScalingFactor(A);

	float D = DiffuseMeanFreePathForSampling / S;
	float CenterSampleRadiusCdf = GetCDF(D.x,CenterSampleRadiusInMM,0);

	return CenterSampleRadiusCdf;
}

void UpdateSeed(int3 Seed3D, inout int StartSeed)
{
	/*To make R2Sequence work, we need to rebase the R2 sequence start index to a new one uniformly in the R2 space,
	  then sample sequentially for the current frame. With this mechanism, we can get the best
	  quality for each frame, and thus best over time.*/
	StartSeed = Rand3DPCG16(int3(Seed3D.xy, StartSeed)).x;
}

FBurleySampleInfo GenerateSampleInfo(float2 Rand0T1, float DiffuseMeanFreePathForSample, float SpectralForSample, uint SequenceId)
{
	FBurleySampleInfo BurleySampleInfo;

	// Direct sampling of angle is more efficient and fast in test when the dmfp is small.
	// However, FIB has better quality when dmfp and world unit scale is large.


	//Approximation
	float FoundRoot = RadiusRootFindByApproximation(DiffuseMeanFreePathForSample / SpectralForSample, Rand0T1.x);

	BurleySampleInfo.RadiusInMM = max(FoundRoot, 0.00001f);
	
	// Sample angle
	BurleySampleInfo.Theta = Rand0T1.y * 2 * Math::PI;

	BurleySampleInfo.CosTheta = cos(BurleySampleInfo.Theta);
	BurleySampleInfo.SinTheta = sin(BurleySampleInfo.Theta);

	// Estimate Pdf
	BurleySampleInfo.Pdf = GetPdf(BurleySampleInfo.RadiusInMM, DiffuseMeanFreePathForSample, SpectralForSample);

	return BurleySampleInfo;
}

float GetDepth(uint2 DTid, float scale)
{
	float Depth = DepthTexture[DTid].x;
	Depth = SharedData::GetScreenDepth(Depth) * scale;
	return Depth;
}

float4 BurleyNormalizedSS(uint2 DTid, float2 texCoord, float sssAmount, bool humanProfile) {
    float4 profile = humanProfile ? HumanProfile : BaseProfile;
	float scale = pow(10, profile.y * 10 - 15);
	BurleySampleDiffuseNormal CenterSample = SampleSSSColorConsideringLocalShared(texCoord, 0, DTid, 0);
    float DepthAtDiscCenter = GetDepth(DTid, scale);

    float3 OriginalColor = CenterSample.DiffuseLighting.rgb;
    float4 OutColor = 0;

    [branch] if (sssAmount == 0)
	{
		return ColorTexture[DTid.xy];
	}

    const float3 WorldNormal = NormalTexture[DTid.xy].xyz;
    FBurleyParameter BurleyParameter = (FBurleyParameter)0;
	BurleyParameter.SurfaceAlbedo = AlbedoTexture[DTid.xy];
	BurleyParameter.DiffuseMeanFreePath = float4(0.6f, 0.3f, 0.1f, 1.0f);
	BurleyParameter.WorldUnitScale = 1.0f;
	BurleyParameter.SurfaceOpacity = sssAmount;
	float3 debugColor = DepthAtDiscCenter.xxx;
    float DiffuseMeanFreePathForSampling = GetDiffuseMeanFreePathForSampling(BurleyParameter.DiffuseMeanFreePath);
	// float A = GetComponentForScalingFactorEstimation(BurleyParameter.SurfaceAlbedo);
	float A = profile.x;
	float3 BoundaryColorBleed = GetSubsurfaceProfileBoundaryColorBleed(1).xyz;

    float S = GetScalingFactor(A);
	float3 S3D = GetScalingFactor3D(BurleyParameter.SurfaceAlbedo.xyz);

    uint SeedStart = SharedData::FrameCount;
	float3 WeightingFactor = 0.0f;
	float4 RadianceAccumulated = float4(0.0f, 0.0f, 0.0f, 1.0f);
	float Mask = 1.0f;
	float3 BoundaryColorBleedAccum = float3(0.0f, 0.0f, 0.0f);

    int NumOfSamples = BURLEY_NUM_SAMPLES;
	float InvNumOfSamples = BURLEY_INV_NUM_SAMPLES;

    const int SSSOverrideNumSamples = 8;
	if (SSSOverrideNumSamples > 0)
	{
		NumOfSamples = SSSOverrideNumSamples;
		InvNumOfSamples = 1.0f / float(SSSOverrideNumSamples);
	}

	int3 Seed3D = int3(texCoord * SeedStart, 0);

	UpdateSeed(Seed3D, SeedStart);

    float2 BurleyScale = CalculateBurleyScale(BurleyParameter.WorldUnitScale,DepthAtDiscCenter);

	/*************************************************************************************
	 * Center Sample Reweighting
	 * 
	 * The original burley algorithm involes monte car sampling. Given a random variable [0,1],
	 * find the distance of that point from the center using the CDF, and then divide by PDF. 
	 * But it is somewhat inefficient because it is weighted heavily towards the center.
	 *
	 * Instead, we are going to split the [0,1] random variable range. First, we figure out the
	 * radius (R) of the center sample in world space. Second, we are going to determine the random
	 * variable (T) such that CDF(R) = T. Then we split the range into two segments.
	 *
	 * 1. The center sample, which include the random variable values from [0,T].
	 * 2. All other samples, which include the random variable values from [T,1].
	 *
	 * With the center sample is scaled the weight T and the rest of the samples are weighted
	 * by (1-T). There shouldn't be any bias, except for small errors due to precision.
	 **************************************************************************************/

	float CenterSampleRadiusCdf = CalculateCenterSampleCdf(BurleyParameter, DepthAtDiscCenter, texCoord);
	float3 CenterSampleWeight = CalculateCenterSampleWeight(DepthAtDiscCenter, BurleyParameter, texCoord);

    for (int i = 0; i < NumOfSamples; ++i)
	{
		// Step 1: sample generation
		// Create an 2d disk sampling pattern (we can load from the disk as a texture or buffer).
		Seed3D.z = SeedStart++;
		float2 Random0T1 = Generate2DRandomNumber(Seed3D);

		// The random variable goes from 0 to 1. CenterSampleRadiusCdf is the probability that a sample hits the
		// center pixel. Since that probability is accounted for in the lighting, we only sample in the
		// range [CenterSampleRadiusCdf,1] instead of [0,1]
		Random0T1.x = CenterSampleRadiusCdf + Random0T1.x*(1.0f - CenterSampleRadiusCdf);

        FBurleySampleInfo BurleySampleInfo = GenerateSampleInfo(Random0T1, DiffuseMeanFreePathForSampling, S, i);

        // Step 2: get the light radiance and depth at the offset
		// and estimate the scale from the random disk sampling space to sceen space.
		
		// World unit to screen space unit
		float2 UVOffset = BurleyScale*BurleySampleInfo.RadiusInMM;
		UVOffset.x *= BurleySampleInfo.CosTheta;
		UVOffset.y *= BurleySampleInfo.SinTheta;

        // Sampling
		{
			float2 SampledDiscUV = texCoord + UVOffset;
			uint2 SampledDTid = uint2(saturate(SampledDiscUV) / SharedData::BufferDim.zw - 0.5f);

			float3 SampledDiffuse = ColorTexture[SampledDTid].xyz;
			float SampledDepth = GetDepth(SampledDTid, scale);
			SampledDepth = SharedData::GetScreenDepth(SampledDepth);
			float4 SampledRadianceAndDepth = float4(SampledDiffuse, SampledDepth);

			const float3 SampleWorldNormal = NormalTexture[SampledDTid].xyz;

            // Step 3: Get weight from normal similarity
			float NormalWeight = sqrt(saturate(dot(SampleWorldNormal,WorldNormal)*.5f + .5f));

            // Step 4: create the bilateral filtering weighted Distance between entry and exit.
            // Bring DeltaDepth into the normalized kernal space.
			// 
			// Without the division of world unit scale, we add too much penalty to the sample weight when world unit scale is 
			// large. E.g., when we have a 1 cm world unit scale (i.e., 1cm is regarded as 1mm), if we get 1mm depth difference, 
			// it should be treated as 0.1mm instead of 1mm to reduce the weight contribution.
			float DeltaDepth = (SampledRadianceAndDepth.w - DepthAtDiscCenter) * BURLEY_CM_2_MM / BurleyParameter.WorldUnitScale;
			float RadiusSampledInMM = sqrt(BurleySampleInfo.RadiusInMM * BurleySampleInfo.RadiusInMM + DeltaDepth * DeltaDepth);
            BurleySampleInfo.Pdf = GetPdf(RadiusSampledInMM, DiffuseMeanFreePathForSampling, S);

			// Determine the tint color, if the sampling pixel is not subsurface, we use tint color
			// to mask out the sampling. Unless we specifically want the shadowing region.
			BoundaryColorBleedAccum += BoundaryColorBleed;

			// Step 4: accumulate radiance from the diffusion profile rR(r)
			// make sure the DiffuseMeanFreePath is not zero and in mm.
			float3 DiffusionProfile = GetDiffuseReflectProfileWithDiffuseMeanFreePath(BurleyParameter.DiffuseMeanFreePath.xyz, S3D.xyz, RadiusSampledInMM);
			float3 SampleWeight = (DiffusionProfile / BurleySampleInfo.Pdf) * NormalWeight;

			RadianceAccumulated.xyz += SampleWeight * (SampledRadianceAndDepth.xyz);

            WeightingFactor += SampleWeight;
        }
	}

    // 0.99995f is a compensitation to make it energe conservation.
    RadianceAccumulated.xyz *= 0.99995f;

    RadianceAccumulated.xyz *= BoundaryColorBleedAccum * InvNumOfSamples;

	// Apply lerp with center pixel
	RadianceAccumulated.xyz = lerp(RadianceAccumulated.xyz,OriginalColor,CenterSampleWeight);

    // The opacity works by reducing the radius based on opacity, but this runs into precision issues with low opacity values.
	// So as the opacity goes to SSSS_OPACITY_THRESHOLD_EPS, we transition to fully disabling SSS by the time we get there.
	float LowOpacityEps = SSSS_OPACITY_THRESHOLD_EPS;

	float OriginalLerp = saturate((BurleyParameter.SurfaceOpacity - LowOpacityEps) / LowOpacityEps);

	OutColor.xyz = lerp(OriginalColor,RadianceAccumulated.xyz,OriginalLerp);
	// OutColor.xyz = debugColor;
	OutColor.w = ColorTexture[DTid.xy].w;

	return OutColor;
}