#ifndef __GBUFFER_DEPENDENCY_HLSL__
#define __GBUFFER_DEPENDENCY_HLSL__

namespace GBuffer
{

	float2 EncodeNormal(float3 n)
	{
		n.z = max(0.001, sqrt(8.0 - 8.0 * n.z));
		n.xy /= n.z;
		return n.xy + 0.5;
	}

	float3 DecodeNormal(float2 enc)
	{
		float2 fenc = enc * 4.0 - 2.0;
		float f = dot(fenc, fenc);
		float3 n;
		n.xy = fenc * sqrt(1.0 - f / 4.0);
		n.z = f / 2.0 - 1.0;
		return n;
	}

}

#endif  // __GBUFFER_DEPENDENCY_HLSL__