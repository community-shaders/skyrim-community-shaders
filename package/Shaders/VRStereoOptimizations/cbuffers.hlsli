// VR Stereo Optimizations - Shared constant buffer layout
// Must match VRStereoOptParams in VRStereoOptimizations.h exactly

#ifndef __VR_STEREO_OPT_CBUFFERS_HLSLI__
#define __VR_STEREO_OPT_CBUFFERS_HLSLI__

cbuffer VRStereoOptParams : register(b1)
{
	float2 FrameDim;     // Full stereo buffer dimensions (both eyes)
	float2 RcpFrameDim;  // 1.0 / FrameDim

	uint StereoModeValue;         // 0=Off, 1=Enable
	float DisocclusionThreshold;  // Depth difference threshold for disocclusion detection
	float EdgeDepthThreshold;     // Relative depth difference threshold for edge detection
	uint EdgeWidth;               // Half-width of edge detection band in pixels

	float2 QualityJitter;  // Sub-pixel jitter offset (Quality mode)
	float FoveatedRadius;  // Radius of foveal region in UV space
	float pad2;

	float2 FoveatedCenter;  // Center of foveal region in UV space
	float MinEdgeDistance;
	float FullBlendDistance;  // Linearized depth below which pixels get MODE_FULL_BLEND (game units)
};

#define STEREO_MODE_OFF 0
#define STEREO_MODE_ENABLE 1

#define MODE_DISOCCLUDED 0     // Fully shaded, no reprojection, no blend (sky, HMD mask, parallax-occluded)
#define MODE_EDGE 1            // Depth edge boundary (distance 1) or inner/foreground band; fully shaded + bilateral blend
#define MODE_MAIN 2            // Eye 0: no reproject (Perf) / bilateral (Quality). Eye 1: overwrite reproject (Perf) / bilateral (Quality)
#define MODE_EDGE_NEIGHBOUR 3  // (Legacy, unused) Outer/background band — now classified as MODE_MAIN
#define MODE_FULL_BLEND 4      // Near-camera geometry: both eyes fully shaded + bilateral blend for 2x supersampling

#endif
