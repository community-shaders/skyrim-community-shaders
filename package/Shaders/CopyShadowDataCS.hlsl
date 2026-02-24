// Copies shadow data from Skyrim's constant buffers into a structured buffer accessible by all shaders.
// Supports directional (RENDER_SHADOWMASK), spot (RENDER_SHADOWMASKSPOT),
// and paraboloid (RENDER_SHADOWMASKPB / RENDER_SHADOWMASKDPB) shadow types.
// Currently only populates directional shadow data (index 0).

struct ShadowData
{
	float4 VPOSOffset;
	float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances;    // cascade end distances in xyz, cascade count in w
	float4 StartSplitDistances;  // cascade start distances in xyz, 4 in w
	float4 FocusShadowFadeParam;
	float4 DebugColor;
	float4 PropertyColor;
	float4 AlphaTestRef;
	float4 ShadowLightParam;       // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4];  // Focus (near) shadow projections — always affine
	// float4x4 supports directional (affine, expanded from float4x3) and spot/paraboloid (perspective)
	float4x4 ShadowMapProj[2][3];
	float4x4 CameraViewProjInverse[2];
};

cbuffer PerFrame : register(b0)
{
	float4 VPOSOffset : packoffset(c0);
	float4 ShadowSampleParam : packoffset(c1);    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances : packoffset(c2);    // cascade end distances in xyz, cascade count in w
	float4 StartSplitDistances : packoffset(c3);  // cascade start distances in xyz, 4 in w
	float4 FocusShadowFadeParam : packoffset(c4);
}

cbuffer PerFrame2 : register(b1)
{
#if !defined(VR)
	row_major float4x4 CameraView[1] : packoffset(c0);
	row_major float4x4 CameraProj[1] : packoffset(c4);
	row_major float4x4 CameraViewProj[1] : packoffset(c8);
	row_major float4x4 CameraViewProjUnjittered[1] : packoffset(c12);
	row_major float4x4 CameraPreviousViewProjUnjittered[1] : packoffset(c16);
	row_major float4x4 CameraProjUnjittered[1] : packoffset(c20);
	row_major float4x4 CameraProjUnjitteredInverse[1] : packoffset(c24);
	row_major float4x4 CameraViewInverse[1] : packoffset(c28);
	row_major float4x4 CameraViewProjInverse[1] : packoffset(c32);
	row_major float4x4 CameraProjInverse[1] : packoffset(c36);
	float4 CameraPosAdjust[1] : packoffset(c40);
	float4 CameraPreviousPosAdjust[1] : packoffset(c41);  // fDRClampOffset in w
	float4 FrameParams : packoffset(c42);                 // inverse fGamma in x, some flags in yzw
	float4 DynamicResolutionParams1 : packoffset(c43);
	float4 DynamicResolutionParams2 : packoffset(c44);
#else
	row_major float4x4 CameraView[2] : packoffset(c0);
	row_major float4x4 CameraProj[2] : packoffset(c8);
	row_major float4x4 CameraViewProj[2] : packoffset(c16);
	row_major float4x4 CameraViewProjUnjittered[2] : packoffset(c24);
	row_major float4x4 CameraPreviousViewProjUnjittered[2] : packoffset(c32);
	row_major float4x4 CameraProjUnjittered[2] : packoffset(c40);
	row_major float4x4 CameraProjUnjitteredInverse[2] : packoffset(c48);
	row_major float4x4 CameraViewInverse[2] : packoffset(c56);
	row_major float4x4 CameraViewProjInverse[2] : packoffset(c64);
	row_major float4x4 CameraProjInverse[2] : packoffset(c72);
	float4 CameraPosAdjust[2] : packoffset(c80);
	float4 CameraPreviousPosAdjust[2] : packoffset(c82);  // fDRClampOffset in w
	float4 FrameParams : packoffset(c84);                 // inverse fGamma in x, some flags in yzw
	float4 DynamicResolutionParams1 : packoffset(c85);
	float4 DynamicResolutionParams2 : packoffset(c86);
#endif  // !VR
}

// Copied from UtilShader(b2). Layout matches RENDER_SHADOWMASK (directional cascaded shadows).
cbuffer PerFrame3 : register(b2)
{
	float4 DebugColor : packoffset(c0);
	float4 PropertyColor : packoffset(c1);
	float4 AlphaTestRef : packoffset(c2);
	float4 ShadowLightParam : packoffset(c3);  // Falloff in x, ShadowDistance squared in z
#if !defined(VR)
	float4x3 FocusShadowMapProj[4] : packoffset(c4);
	float4x3 ShadowMapProj[1][3] : packoffset(c16);  // 16, 19, 22
#else
	float4 VRUnknown : packoffset(c4);  // used to multiply by identity matrix
	float4x3 FocusShadowMapProj[4] : packoffset(c5);
	float4x3 ShadowMapProj[2][3] : packoffset(c29);  // VR: {29, 32, 35} and {38, 41, 44}
#endif  // VR
}

RWStructuredBuffer<ShadowData> ShadowDataBuffer : register(u0);

// Expands an affine (orthographic) float4x3 shadow transform to float4x4.
// The 4th column is set to (0, 0, 0, 1) so that perspective divide after
// mul(transpose(M), pos4) yields w = 1, preserving the affine result.
float4x4 ExpandAffine(float4x3 m)
{
	return float4x4(
		float4(m[0], 0),  // row 0: xyz from float3, w = 0
		float4(m[1], 0),  // row 1
		float4(m[2], 0),  // row 2
		float4(m[3], 1)   // row 3 (translation): w = 1 for homogeneous coordinates
	);
}

[numthreads(1, 1, 1)] void main()
{
	ShadowData sd;
	sd.DebugColor = DebugColor;
	sd.PropertyColor = PropertyColor;
	sd.AlphaTestRef = AlphaTestRef;
	sd.ShadowLightParam = ShadowLightParam;
	sd.FocusShadowMapProj = FocusShadowMapProj;

	sd.CameraViewProjInverse[0] = CameraViewProjInverse[0];

	// Expand directional (affine) shadow projections from float4x3 to float4x4
	[unroll]
	for (int i = 0; i < 3; i++)
		sd.ShadowMapProj[0][i] = ExpandAffine(ShadowMapProj[0][i]);

#if defined(VR)
	sd.CameraViewProjInverse[1] = CameraViewProjInverse[1];

	[unroll]
	for (int j = 0; j < 3; j++)
		sd.ShadowMapProj[1][j] = ExpandAffine(ShadowMapProj[1][j]);
#else
	sd.CameraViewProjInverse[1] = CameraViewProjInverse[0];

	// Non-VR: duplicate eye 0 data into eye 1 slot
	[unroll]
	for (int j = 0; j < 3; j++)
		sd.ShadowMapProj[1][j] = sd.ShadowMapProj[0][j];
#endif  // VR

	sd.VPOSOffset = VPOSOffset;
	sd.ShadowSampleParam = ShadowSampleParam;
	sd.EndSplitDistances = EndSplitDistances;
	sd.StartSplitDistances = StartSplitDistances;
	sd.FocusShadowFadeParam = FocusShadowFadeParam;

	ShadowDataBuffer[0] = sd;
}
