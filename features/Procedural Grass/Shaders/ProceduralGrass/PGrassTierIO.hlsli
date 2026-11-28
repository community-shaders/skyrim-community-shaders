#ifndef __PGRASS_TIER_IO_HLSLI__
#define __PGRASS_TIER_IO_HLSLI__

struct GrassTierIO
{
#if defined(VSHADER)
	precise float4 Position: SV_POSITION;
#else
	float4 Position: SV_POSITION;
#endif

#if defined(DEPTH)
#	if defined(DEPTH_CLIP)
	float BladeHeight: TEXCOORD0;
#	endif

#else
	float4 CameraRelativePosition: TEXCOORD0;  // xyz: camera-relative position; w: across-blade coordinate

#	if defined(HIGH_LOD)
	float4 PreviousCameraRelativePosition: TEXCOORD1;  // xyz: previous camera-relative position; w: Bezier t
#	elif defined(LOW_LOD)
	float BladeT: TEXCOORD1;
#	endif

#	if defined(HIGH_LOD)
	nointerpolation float3 WindLodDensity: TEXCOORD2;  // xy: tip wind offset; z: canopy density and shadow
#	elif defined(MID_LOD)
	nointerpolation float4 WindRootPosition: TEXCOORD2;  // xy: tip wind offset; zw: root camera-relative XY
#	elif defined(LOW_LOD)
	nointerpolation float4 RootPosition: TEXCOORD2;  // xy: camera-relative blade root XY; zw: root pixel for the shadow mask
#	endif

#	if defined(MID_LOD)
	float3 BladeTDepth: TEXCOORD3;                 // x: Bezier t; y: positive view depth; z: root camera-relative height
	nointerpolation uint MaterialData: TEXCOORD7;  // clump seed/density and double-blade flag
#	elif !defined(LOW_LOD)
	float4 AOThicknessRoughness: TEXCOORD3;  // xyz: AO, thickness, roughness; w: root-relative height
#	endif

#	if defined(LOW_LOD)
	nointerpolation float2 BezierTipAndMid: TEXCOORD4;  // Low reconstructs the midpoint in the pixel shader.
#	else
	nointerpolation float4 BezierTipAndMid: TEXCOORD4;  // xy: tip; zw: midpoint in facing/up space
#	endif

	nointerpolation float4 BladeParams: TEXCOORD5;  // xy: facing; z: type; w: tier-specific packed data

#	if !defined(LOW_LOD) && !defined(MID_LOD)
	float4 BaseToTipColor: TEXCOORD7;  // xyz: blade colour; w: positive view depth
#	endif

#	if defined(SKYLIGHTING) && !defined(LOW_LOD)
#		if defined(MID_LOD)
	nointerpolation float3 SkylightingRoot: TEXCOORD9;  // Full-precision probe position from the generator.
#		else
	nointerpolation float4 SkylightingVertexSH: TEXCOORD9;  // Per-blade SH from the generator.
#		endif
#	endif
#endif
};

#endif
