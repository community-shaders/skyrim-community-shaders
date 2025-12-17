#ifndef PBR_HLSL
#define PBR_HLSL

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Math.hlsli"

namespace PBR
{
	namespace Flags
	{
		static const uint HasEmissive = (1 << 0);
		static const uint HasDisplacement = (1 << 1);
		static const uint HasFeatureTexture0 = (1 << 2);
		static const uint HasFeatureTexture1 = (1 << 3);
		static const uint Subsurface = (1 << 4);
		static const uint TwoLayer = (1 << 5);
		static const uint ColoredCoat = (1 << 6);
		static const uint InterlayerParallax = (1 << 7);
		static const uint CoatNormal = (1 << 8);
		static const uint Fuzz = (1 << 9);
		static const uint HairMarschner = (1 << 10);
		static const uint Glint = (1 << 11);
		static const uint ProjectedGlint = (1 << 12);
	}
}

#endif  // PBR_HLSL