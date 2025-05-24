
#include "Common/SharedData.hlsli"

#include "ScreenSpaceShadows/bend_sss_gpu.hlsli"

Texture2D<unorm half> DepthTexture : register(t0);     // Depth Buffer Texture (rasterized non-linear depth)
RWTexture2D<unorm half> OutputTexture : register(u0);  // Output screen-space shadow buffer (typically single-channel, 8bit)
SamplerState PointBorderSampler : register(s0);        // A point sampler, with Wrap Mode set to Clamp-To-Border-Color (D3D12_TEXTURE_ADDRESS_MODE_BORDER), and Border Color set to "FarDepthValue" (typically zero), or some other far-depth value out of DepthBounds.
													   // If you have issues where invalid shadows are appearing from off-screen, it is likely that this sampler is not correctly setup
cbuffer PerFrame : register(b1)
{
	// Runtime data returned from BuildDispatchList():
	float4 LightCoordinate;  // Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
	int2 WaveOffset;         // Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

	// Renderer Specific Values:
	float FarDepthValue;   // Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
	float NearDepthValue;  // Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

	// Sampling data:
	float2 InvDepthTextureSize;  // Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
								 // If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
								 // The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

	float2 DynamicRes;

	float SurfaceThickness;
	float BilinearThreshold;
	float ShadowContrast;
};

[numthreads(WAVE_SIZE, 1, 1)] void main(
	int3 groupID
	: SV_GroupID,
	int groupThreadID
	: SV_GroupThreadID) {
	DispatchParameters parameters;
	parameters.SetDefaults();

	parameters.LightCoordinate = LightCoordinate;
	parameters.WaveOffset = WaveOffset;
	parameters.FarDepthValue = 1;
	parameters.NearDepthValue = 0;
	parameters.InvDepthTextureSize = InvDepthTextureSize;
	parameters.DepthTexture = DepthTexture;
	parameters.OutputTexture = OutputTexture;
	parameters.PointBorderSampler = PointBorderSampler;

	parameters.SurfaceThickness = SurfaceThickness;
	parameters.BilinearThreshold = BilinearThreshold;
	parameters.ShadowContrast = ShadowContrast;

	parameters.DynamicRes = DynamicRes;

	WriteScreenSpaceShadow(parameters, groupID, groupThreadID);
}