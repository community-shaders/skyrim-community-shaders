#include "Common/SharedData.hlsli"

Texture2D<float2> TAAMask : register(t0);

Texture2D<float4> NormalsWaterMask : register(t1);

RWTexture2D<float> ReactiveMask : register(u0);

#if defined(DLSS) || defined(FSR)
RWTexture2D<float> TransparencyCompositionMask : register(u1);
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float2 taaMask = TAAMask[dispatchID.xy];

#if defined(DLSS)
	float reactiveMask = taaMask.x + taaMask.y;
#else
	float reactiveMask = taaMask.x * 0.01 + taaMask.y;
#endif

	float transparencyCompositionMask = NormalsWaterMask[dispatchID.xy].z;

#if defined(DLSS)
	ReactiveMask[dispatchID.xy] = reactiveMask;
	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
#elif defined(FSR)
	ReactiveMask[dispatchID.xy] = reactiveMask;
	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
#else
	ReactiveMask[dispatchID.xy] = reactiveMask + transparencyCompositionMask * 0.1;
#endif

}
