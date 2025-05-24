#ifndef __GBUFFER_DEPENDENCY_HLSL__
#define __GBUFFER_DEPENDENCY_HLSL__

// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/

namespace GBuffer
{

	half2 OctWrap(half2 v)
	{
		return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
	}

	half2 EncodeNormal(half3 n)
	{
		n = -n;
		n /= (abs(n.x) + abs(n.y) + abs(n.z));
		n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
		n.xy = n.xy * 0.5 + 0.5;
		return n.xy;
	}

	half3 DecodeNormal(half2 f)
	{
		f = f * 2.0 - 1.0;
		// https://twitter.com/Stubbesaurus/status/937994790553227264
		half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
		half t = saturate(-n.z);
		n.xy += n.xy >= 0.0 ? -t : t;
		return -normalize(n);
	}

	half2 EncodeNormalVanilla(half3 n)
	{
		n.z = max(1.0 / 1000.0, sqrt(8 + -8 * n.z));
		n.xy /= n.z;
		return n.xy + 0.5;
	}

}

#endif  // __GBUFFER_DEPENDENCY_HLSL__