#include "Common/DummyVSTexCoord.hlsl"
#include "Common/VR.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;  // Final color output for the pixel shader.
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);   // Sampler state for texture sampling.
Texture2D<float4> ImageTex : register(t0);  // Texture to sample colors from.

cbuffer PerGeometry : register(b2)
{
	float4 FullScreenColor;  // Color applied to the final output, used for tinting or blending effects.
	float4 Params0;          // General parameters; may include scaling or offset values.
	float4 Params1;          // Length parameters for scaling or thresholding; Params1.z represents `g_flTime`.
	float4 UpsampleParams;   // Dynamic resolution parameters:
							 //   - UpsampleParams.x: fDynamicResolutionWidthRatio
							 //   - UpsampleParams.y: fDynamicResolutionHeightRatio
							 //   - UpsampleParams.z: fDynamicResolutionPreviousWidthRatio
							 //   - UpsampleParams.w: fDynamicResolutionPreviousHeightRatio
};

// Function to generate noise using Valve's ScreenSpaceDither method.
// References:
// - https://blog.frost.kiwi/GLSL-noise-and-radial-gradient/
// - https://media.steampowered.com/apps/valve/2015/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf
float3 ScreenSpaceDither(float2 vScreenPos)
{
	// Iestyn's RGB dither (7 asm instructions) from Portal 2 X360,
	// slightly modified for VR applications.
	float3 vDither = dot(float2(171.0, 231.0), vScreenPos.xy + Params1.zz).xxx;
	vDither.rgb = frac(vDither.rgb / float3(103.0, 71.0, 97.0)) - float3(0.5, 0.5, 0.5);
	return (vDither.rgb / 255.0) * 0.375;  // Normalize dither values to a suitable range.
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 uv = input.TexCoord;  // Get the UV coordinates from input.

	// Convert UV to normalized screen space [-1, 1].
	float2 normalizedScreenCoord = Stereo::ConvertUVToNormalizedScreenSpace(uv);

	// Calculate the length of the normalized screen coordinates.
	float normalizedLength = saturate(Params1.x * (length(normalizedScreenCoord) - Params1.y) * Params0.x);

	// Upsample and clamp texture coordinates based on dynamic resolution.
	float2 uvScaled = min(UpsampleParams.zw, UpsampleParams.xy * uv.xy);  // Clamp UVs to prevent overflow.
	float3 sampledColor = ImageTex.Sample(ImageSampler, uvScaled).xyz;    // Sample color from the texture.

	// Manipulate the sampled color based on the normalized length.
	float3 finalColor = sampledColor * (1.0 + normalizedLength);  // Scale sampled color.

	// Generate noise to apply to the final color.
	float3 noise = ScreenSpaceDither(input.Position.xy);
	finalColor += Params0.yyy * noise * Params1.www;  // Adjust final color with noise.

	// Final color manipulation: blend final color with FullScreenColor.
	psout.Color.xyz = lerp(finalColor, FullScreenColor.xyz, FullScreenColor.www);  // Blend based on the alpha component.
	psout.Color.w = 1.0;                                                           // Set alpha to full opacity.

	return psout;  // Return the pixel shader output.
}
#endif
