
struct PerGeometry
{
	float4 VPOSOffset;
	float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam;
	float4 DebugColor;
	float4 PropertyColor;
	float4 AlphaTestRef;
	float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4];
	// Since PerGeometry is passed between c++ and hlsl, can't have different defines due to strong typing
	float4x3 ShadowMapProj[2][3];
	float4x4 CameraViewProjInverse[2];
};

cbuffer PerFrame : register(b0)
{
	float4 VPOSOffset : packoffset(c0);
	float4 ShadowSampleParam : packoffset(c1);    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances : packoffset(c2);    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances : packoffset(c3);  // cascade start ditances int xyz, 4 int z
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
	float4 DynamicResolutionParams1 : packoffset(c43);    // fDynamicResolutionWidthRatio in x,
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionPreviousWidthRatio in z,
														  // fDynamicResolutionPreviousHeightRatio in w
	float4 DynamicResolutionParams2 : packoffset(c44);    // inverse fDynamicResolutionWidthRatio in x, inverse
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
														  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
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
	float4 DynamicResolutionParams1 : packoffset(c85);    // fDynamicResolutionWidthRatio in x,
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionPreviousWidthRatio in z,
														  // fDynamicResolutionPreviousHeightRatio in w
	float4 DynamicResolutionParams2 : packoffset(c86);    // inverse fDynamicResolutionWidthRatio in x, inverse
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
														  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
#endif  // !VR
}

// copied from UtilShader(b2). Assume RENDER_SHADOWMASK
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
	float4 VRUnknown : packoffset(c4);                    /* used to multiply by identity matrix, see e.g., 4202499.ps.bin.hlsl
									   r1.x = dot(cb2[4].xz, icb[r0.w+0].xz);
									   r1.x = r0.x * cb12[86].x + -r1.x;
									   r0.w = (int)r0.w + 1;
									   r0.w = (int)r0.w + -1;
									   r0.w = dot(cb2[4].yw, icb[r0.w+0].xz);
									   */
	float4x3 FocusShadowMapProj[4] : packoffset(c5);
	float4x3 ShadowMapProj[2][3] : packoffset(c29);  // VR has a couple of offsets of 3, e.g., {29, 32, 35} and {38, 41, 44}, compare to Flat which does [16, 19, 22]
#endif  // VR
}

RWStructuredBuffer<PerGeometry> ShadowData : register(u0);

[numthreads(1, 1, 1)] void main() {
	PerGeometry perGeometry;
	perGeometry.DebugColor = DebugColor;
	perGeometry.PropertyColor = PropertyColor;
	perGeometry.AlphaTestRef = AlphaTestRef;
	perGeometry.ShadowLightParam = ShadowLightParam;
	perGeometry.FocusShadowMapProj = FocusShadowMapProj;
	perGeometry.ShadowMapProj[0] = ShadowMapProj[0];

	perGeometry.CameraViewProjInverse[0] = CameraViewProjInverse[0];
#if defined(VR)
	perGeometry.ShadowMapProj[1] = ShadowMapProj[1];

	perGeometry.CameraViewProjInverse[1] = CameraViewProjInverse[1];
#else
	perGeometry.ShadowMapProj[1] = ShadowMapProj[0];

	perGeometry.CameraViewProjInverse[1] = CameraViewProjInverse[0];
#endif

	perGeometry.VPOSOffset = VPOSOffset;
	perGeometry.ShadowSampleParam = ShadowSampleParam;
	perGeometry.EndSplitDistances = EndSplitDistances;
	perGeometry.StartSplitDistances = StartSplitDistances;
	perGeometry.FocusShadowFadeParam = FocusShadowFadeParam;

	ShadowData[0] = perGeometry;
}