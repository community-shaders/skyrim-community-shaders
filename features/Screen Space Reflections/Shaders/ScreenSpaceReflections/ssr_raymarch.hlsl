#include "ScreenSpaceReflections/ssr_common.hlsli"

Texture2D<float4> ScreenColorTextureMips : register(t3);
Texture2D<float> DepthTexture : register(t4);
Texture2D<float> LinearDepthTextureMips : register(t5);

RWTexture2D<float4> SSRColorOutput : register(u0);
RWTexture2D<float> SSRHitDistanceOutput : register(u1);

cbuffer SSRCB : register(b1)
{
    uint MaxSteps;
    uint NumRays;
    uint Glossy;
    float RoughnessMask;
};

float GetStepScreenFactorToClipAtScreenEdge(float2 RayStartScreen, float2 RayStepScreen)
{
	const float RayStepScreenInvFactor = 0.5 * length(RayStepScreen);
	const float2 S = 1 - max(abs(RayStepScreen + RayStartScreen * RayStepScreenInvFactor) - RayStepScreenInvFactor, 0.0f) / abs(RayStepScreen);
	const float RayStepFactor = min(S.x, S.y) / RayStepScreenInvFactor;
	return RayStepFactor;
}

float GetScreenFadeBord(float2 pos, float value)
{
    float borderDist = min(1 - max(pos.x, pos.y), min(pos.x, pos.y));
    return saturate(borderDist > value ? 1 : borderDist / value);
}

bool RayMarch(float3 rayStartWS, float3 dirWS, float depth,
    float roughness, float maxDistance, uint maxSteps, float startMipLevel, float offset,
    out float3 hitPointUVz, out float mipLevel, out float3 debug, uint eyeIndex = 0)
{
    mipLevel = startMipLevel;
    debug = float3(0, 0, 0);
    hitPointUVz = float3(0, 0, 0);

    float3 rayStartVS = FrameBuffer::WorldToView(rayStartWS, true, eyeIndex);
    float3 dirVS = FrameBuffer::WorldToView(dirWS, false, eyeIndex);

    float4 rayStartClip = mul(FrameBuffer::CameraProj[eyeIndex], float4(rayStartVS, 1));
    float3 rayStartScreen = rayStartClip.xyz / rayStartClip.w;
    float4 rayEndScreen = mul(FrameBuffer::CameraProj[eyeIndex], float4(dirVS, 0)) + rayStartClip;
    rayEndScreen.xyz = rcp(max(rayEndScreen.w, 1e-6f)) * rayEndScreen.xyz;
    float3 rayDepthScreen = 0.5 * (rayStartScreen + mul(FrameBuffer::CameraProj[eyeIndex], float4(0, 0, 1, 0)).xyz);
    float3 rayStepScreen = rayEndScreen.xyz - rayStartScreen;
    rayStepScreen *= GetStepScreenFactorToClipAtScreenEdge(rayStartScreen.xy, rayStepScreen.xy);
    float compareTolerance = max(abs(rayStepScreen.z), (rayStartScreen.z - rayDepthScreen.z) * 2);

    float step = 1.0 / maxSteps;
    compareTolerance *= step;

    float3 rayStartUVz = float3((rayStartScreen.xy * float2(0.5, -0.5) + 0.5), rayStartScreen.z);
	float3 rayStepUVz  = float3(rayStepScreen.xy  * float2(0.5, -0.5), rayStepScreen.z);
    rayStepUVz *= step;
    float3 rayUVz = rayStartUVz + rayStepUVz * offset;

    float lastDiff = 0;
    bool hit = false;

    [loop]
    for (uint i = 0; i < maxSteps; ++i)
    {
        // Vectorized to group fetches
		float4 SampleUV0 = rayUVz.xyxy + rayStepUVz.xyxy * float4( 1, 1, 2, 2 );
		float4 SampleUV1 = rayUVz.xyxy + rayStepUVz.xyxy * float4( 3, 3, 4, 4 );
		float4 SampleZ   = rayUVz.zzzz + rayStepUVz.zzzz * float4( 1, 2, 3, 4 );
		
		// Use lower res for farther samples
		float4 SampleDepth;
		SampleDepth.x = LinearDepthTextureMips.SampleLevel(LinearSampler, (SampleUV0.xy), 0).r;
		SampleDepth.y = LinearDepthTextureMips.SampleLevel(LinearSampler, (SampleUV0.zw), 0).r;
		mipLevel += (8 / maxSteps) * roughness;
		
		SampleDepth.z = LinearDepthTextureMips.SampleLevel(LinearSampler, (SampleUV1.xy), 0).r;
		SampleDepth.w = LinearDepthTextureMips.SampleLevel(LinearSampler, (SampleUV1.zw), 0).r;
		mipLevel += (8 / maxSteps) * roughness;

		float4 DepthDiff = SampleZ - SampleDepth;
		bool4 Hit = abs(DepthDiff + compareTolerance) < compareTolerance;

		[branch] 
        if(any(Hit))
		{
			float DepthDiff0 = DepthDiff[2];
			float DepthDiff1 = DepthDiff[3];
			float Time0 = 3;

			[flatten]  
            if( Hit[2] ) 
			{
				DepthDiff0 = DepthDiff[1];
				DepthDiff1 = DepthDiff[2];
				Time0 = 2;
			}

			[flatten] 
            if( Hit[1] ) 
			{
				DepthDiff0 = DepthDiff[0];
				DepthDiff1 = DepthDiff[1];
				Time0 = 1;
			}

			[flatten] 
            if( Hit[0] ) 
			{
				DepthDiff0 = lastDiff;
				DepthDiff1 = DepthDiff[0];
				Time0 = 0;
			}

			float Time1 = Time0 + 1;
		#if 1
			// Binary search
			for( uint j = 0; j < 4; j++ )
			{
				compareTolerance *= 0.5;

				float  MidTime = 0.5 * ( Time0 + Time1 );
				float3 MidUVz = rayUVz + rayStepUVz * MidTime;
				float  MidDepth = LinearDepthTextureMips.SampleLevel( LinearSampler, MidUVz.xy, mipLevel ).r;
				float  MidDepthDiff = MidUVz.z - MidDepth;

				if( abs( MidDepthDiff + compareTolerance ) < compareTolerance ) {
					DepthDiff1	= MidDepthDiff;
					Time1		= MidTime;
				} else {
					DepthDiff0	= MidDepthDiff;
					Time0		= MidTime;
				}
			}
		#endif
			float TimeLerp = saturate( DepthDiff0 * rcp(DepthDiff0 - DepthDiff1) );
			float IntersectTime = Time0 + TimeLerp;
			hitPointUVz = rayUVz + rayStepUVz * IntersectTime;

			hit = true;
			break;
		}
		lastDiff = DepthDiff.w;
		rayUVz += 4 * rayStepUVz;
    }
    return hit;
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 outColor = float4(0, 0, 0, 0);
    
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

    float3 normalVS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalVS, roughness);

    float3 N = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

    if (roughness > RoughnessMask)
    {
        SSRColorOutput[DTid.xy] = float4(0, 0, 0, 0);
        SSRHitDistanceOutput[DTid.xy] = 0;
        return;
    }

    float depth = DepthTexture[DTid.xy].x;
    float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

    if (depth <= 1e-6f)
    {
        SSRColorOutput[DTid.xy] = float4(0, 0, 0, 0);
        SSRHitDistanceOutput[DTid.xy] = 0;
        return;
    }

    const float3 V = normalize(positionWS.xyz);

    float a = roughness * roughness;
    float a2 = a * a;

    float closestDepthSqr = 1e30f;
    uint maxSteps = min(MaxSteps, 16u);
    uint numRays = min(NumRays, 16u);

    float3 debug = float3(0, 0, 0);

    if (NumRays > 1)
    {
        float2 noise;
        noise.x = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, SharedData::FrameCountAlwaysActive);
        noise.y = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, SharedData::FrameCountAlwaysActive * 167);

        uint2 randomUint = Random::pcg3d(int3(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, SharedData::FrameCountAlwaysActive)).xy;

        float3x3 tangentBasis = GetTangentBasis(N);
        float3 tangentV = mul(tangentBasis, V);

        float count = 0;

        if (roughness < 0.1f)
        {
            maxSteps = min(maxSteps * numRays, 24u);
            numRays = 1;
        }

        for (uint i = 0; i < numRays; ++i)
        {
            float stepOffset = noise.x - 0.5f;
            float2 xi = Hammersley16(i, numRays, randomUint);
            float4 PDF = ImportanceSampleGGX(xi, a2, roughness);
            float3 H = mul(PDF.xyz, tangentBasis);
            float3 L = normalize(2 * dot(-V, H) * H + V);

            float3 hitUVz;
            float mipLevel = 0.0f;

            if (roughness < 0.1f)
            {
                L = reflect(V, N);
            }

            bool hit = RayMarch(positionWS.xyz, L, depth, roughness, depth, maxSteps, 1.0f, stepOffset, hitUVz, mipLevel, debug, eyeIndex);

            if (hit)
            {
                closestDepthSqr = min(closestDepthSqr, hitUVz.z * hitUVz.z);

                float2 hitUV = hitUVz.xy;
                if (hitUV.x < -0.1 || hitUV.y < -0.1 || hitUV.x > 1.1 || hitUV.y > 1.1)
                {
                    break;
                }
                float4 hitColor = ScreenColorTextureMips.SampleLevel(LinearSampler, hitUV, mipLevel);
                outColor += float4(hitColor.xyz, GetScreenFadeBord(hitUV, 0));
                debug = float3(hitUV, mipLevel);
            }
        }

        outColor /= max(numRays, 0.0001f);
    } 
    else
    {
        float stepOffset = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, SharedData::FrameCountAlwaysActive) - 0.5f;
        float3 hitUVz;
        float3 L;
        if (Glossy)
        {
            float2 xi = Hammersley16(0, 1, Random::pcg3d(int3(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, SharedData::FrameCountAlwaysActive)).xy);
            float3x3 tangentBasis = GetTangentBasis(N);
            float3 tangentV = mul(tangentBasis, V);

            float3 H = mul(ImportanceSampleGGX(xi, a2, roughness).xyz, tangentBasis);
            L = normalize(2 * dot(-V, H) * H + V);
        } 
        else 
        {
            L = reflect(V, N);
        }

        float mipLevel = 0.0f;
        bool hit = RayMarch(positionWS.xyz, L, depth, roughness, depth, maxSteps, 1.0f, stepOffset, hitUVz, mipLevel, debug, eyeIndex);

        if (hit)
        {
            closestDepthSqr = hitUVz.z * hitUVz.z;

            float2 hitUV = hitUVz.xy;
            if (hitUV.x < -0.1 || hitUV.y < -0.1 || hitUV.x > 1.1 || hitUV.y > 1.1)
            {
                SSRColorOutput[DTid.xy] = float4(0, 0, 0, 0);
                SSRHitDistanceOutput[DTid.xy] = 0;
                return;
            }
            float4 hitColor = ScreenColorTextureMips.SampleLevel(LinearSampler, hitUV, mipLevel);
            outColor += float4(hitColor.xyz, GetScreenFadeBord(hitUV, 0));
            debug = float3(hitUV, mipLevel);
        }
    }
    SSRColorOutput[DTid.xy] = outColor;
    // SSRColorOutput[DTid.xy] = float4(debug.xy,0, 1);
    SSRHitDistanceOutput[DTid.xy] = sqrt(closestDepthSqr);
}