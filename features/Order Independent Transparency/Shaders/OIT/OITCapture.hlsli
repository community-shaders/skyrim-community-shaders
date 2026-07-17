#ifndef __OIT_CAPTURE__
#define __OIT_CAPTURE__

#include "OIT/OITCommon.hlsli"

#include "Common/SharedData.hlsli"
#include "Common/Permutation.hlsli"

#if OIT == 1
	#include "OIT/FragmentList.hlsli"
#elif OIT == 2
	#include "OIT/AOIT.hlsli"
#elif OIT == 3
	#include "OIT/WBOIT.hlsli"
#endif

#if OIT == 3
WBOITResult OIT_CaptureWBOIT(in int2 screenAddress, in float4 color, in float depth)
{
	uint flags = Permutation::ExtraFeatureDescriptor >> OIT_FEATURE_FLAGS_SHIFT;
#if OIT_WRITE_DEPTH
	flags |= OIT_FLAGS_DEPTH_WRITE; // Force enable depth write (for lighting shader right now)
#endif	
WBOITResult empty;
	empty.accumAll = 0.0.xxxx;
	empty.revealage = 1.0.xxxx;
	empty.accumFront = 0.0.xxxx;

	uint blend = flags & OIT_FLAGS_BLEND_MODS;
	if (((blend & OIT_FLAGS_MULTIPLICATIVE) && (SharedData::orderIndependentTransparencySettings.Flags == 0))
		|| (flags & OIT_FLAGS_DISABLED))
		return empty;

	[branch]
	if (SharedData::orderIndependentTransparencySettings.DepthThreshold < 1.0f) {
		if (depth > SharedData::orderIndependentTransparencySettings.DepthThreshold)
			return empty;
	}

	return WBOITCapture(color, screenAddress, depth, flags);
}
#else
float4 OIT_Capture(in int2 screenAddress, in float4 color, in float depth)
{
	uint flags = Permutation::ExtraFeatureDescriptor >> OIT_FEATURE_FLAGS_SHIFT;
#if OIT_WRITE_DEPTH
	flags |= OIT_FLAGS_DEPTH_WRITE; // Force enable depth write (for lighting shader right now)
#endif
	uint blend = flags & OIT_FLAGS_BLEND_MODS;
	// uniform branching to skip OIT for multiplicative blend when not supported (Settings.Flags is only this option at the moment)
	// uniform branching to skip OIT when disabled for objects (too far)
	[branch]
	if (((blend & OIT_FLAGS_MULTIPLICATIVE) && (SharedData::orderIndependentTransparencySettings.Flags == 0))
		|| (flags & OIT_FLAGS_DISABLED))
		return color;

	[branch]
	if (SharedData::orderIndependentTransparencySettings.DepthThreshold < 1.0f)
	{
		// discard pixels that are too far, only if the setting is enabled
		if (depth > SharedData::orderIndependentTransparencySettings.DepthThreshold)
			return color;
	}

	float4 zero = (blend & OIT_FLAGS_MULTIPLICATIVE) ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0);
	
	float4 incolor = color;

#if !OIT_CAPTURE_IGNORE_ALPHA_THRESHOULD
	color.w = max(0.f, color.w - SharedData::orderIndependentTransparencySettings.AlphaThreshold) / (1.f - SharedData::orderIndependentTransparencySettings.AlphaThreshold);
#endif

	if (blend == OIT_FLAGS_MULTIPLICATIVE_A)
		color = float4(0, 0, 0, (1 - color.w) * color.a);
	else if (blend == OIT_FLAGS_ADDITIVE) // additive blend
		color = float4(color.xyz * color.w, 0.f);
	else if (blend == OIT_FLAGS_MULTIPLICATIVE) // multiplicative blend, fully supported needs per channel alpha
		color = float4(0, 0, 0, 1 - color.a);
	else // regular alpha blend
		color = float4(color.xyz * color.w, color.w);
	
	if (OIT_CaptureImpl(screenAddress, color, depth, flags))
		return zero;
	else
		return incolor;
}
#endif

#endif
