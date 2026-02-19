#ifndef __VR_DEPENDENCY_HLSL__
#define __VR_DEPENDENCY_HLSL__
#ifdef VR


// First person model depth threshold for VR occlusion logic
#ifndef VR_FP_Z
#define VR_FP_Z 18.0
#endif


#	if defined(VSHADER)
#		include "Common/Math.hlsli"
#	endif  // VSHADER

#	if (!defined(COMPUTESHADER) && !defined(CSHADER)) || defined(FRAMEBUFFER)
#		include "Common/FrameBuffer.hlsli"
#	endif
cbuffer VRValues : register(b13)
{
	float AlphaTestRefRS : packoffset(c0);
	float StereoEnabled : packoffset(c0.y);
	float2 EyeOffsetScale : packoffset(c0.z);
	float4 EyeClipEdge[2] : packoffset(c1);
}
#endif

namespace Stereo
{
	/**
	Converts to the eye specific uv [0,1].
	In VR, texture buffers include the left and right eye in the same buffer. Flat
	only has a single camera for the entire width. This means the x value [0, .5]
	represents the left eye, and the x value (.5, 1] are the right eye. This returns
	the adjusted value
	@param uv - uv coords [0,1] to be encoded for VR
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@param a_invertY Whether to invert the Y direction
	@returns uv with x coords adjusted for the VR texture buffer
	*/
	float2 ConvertToStereoUV(float2 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
#ifdef VR
		// convert [0,1] to eye specific [0,.5] and [.5, 1] dependent on a_eyeIndex
		uv.x = saturate(uv.x);
		uv.x = (uv.x + (float)a_eyeIndex) / 2;
		if (a_invertY)
			uv.y = 1 - uv.y;
#endif
		return uv;
	}

	float3 ConvertToStereoUV(float3 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertToStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	float4 ConvertToStereoUV(float4 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertToStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	/**
	Converts from eye specific uv to general uv [0,1].
	In VR, texture buffers include the left and right eye in the same buffer.
	This means the x value [0, .5] represents the left eye, and the x value (.5, 1] are the right eye.
	This returns the adjusted value
	@param uv - eye specific uv coords [0,1]; if uv.x < 0.5, it's a left eye; otherwise right
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@param a_invertY Whether to invert the Y direction
	@returns uv with x coords adjusted to full range for either left or right eye
	*/
	float2 ConvertFromStereoUV(float2 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
#ifdef VR
		// convert [0,.5] to [0, 1] and [.5, 1] to [0,1]
		uv.x = 2 * uv.x - (float)a_eyeIndex;
		if (a_invertY)
			uv.y = 1 - uv.y;
#endif
		return uv;
	}

	float3 ConvertFromStereoUV(float3 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertFromStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	float4 ConvertFromStereoUV(float4 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertFromStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	/**
	Converts to the eye specific screenposition [0,Resolution].
	In VR, texture buffers include the left and right eye in the same buffer. Flat only has a single camera for the entire width.
	This means the x value [0, resx/2] represents the left eye, and the x value (resx/2, x] are the right eye.
	This returns the adjusted value
	@param screenPosition - Screenposition coords ([0,resx], [0,resy]) to be encoded for VR
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@param a_resolution The resolution of the screen
	@returns screenPosition with x coords adjusted for the VR texture buffer
	*/
	float2 ConvertToStereoSP(float2 screenPosition, uint a_eyeIndex, float2 a_resolution)
	{
		screenPosition.x /= a_resolution.x;
		float2 stereoUV = ConvertToStereoUV(screenPosition, a_eyeIndex);
		return stereoUV * a_resolution;
	}

	float3 ConvertToStereoSP(float3 screenPosition, uint a_eyeIndex, float2 a_resolution)
	{
		float2 xy = screenPosition.xy / a_resolution;
		xy = ConvertToStereoUV(xy, a_eyeIndex);
		return float3(xy * a_resolution, screenPosition.z);
	}

	float4 ConvertToStereoSP(float4 screenPosition, uint a_eyeIndex, float2 a_resolution)
	{
		float2 xy = screenPosition.xy / a_resolution;
		xy = ConvertToStereoUV(xy, a_eyeIndex);
		return float4(xy * a_resolution, screenPosition.zw);
	}

	/**
	Gets the eyeIndex for Compute Shaders
	@param texCoord Texcoord on the screen [0,1]
	@returns eyeIndex (0 left, 1 right)
	*/
	uint GetEyeIndexFromTexCoord(float2 texCoord)
	{
#ifdef VR
		return (texCoord.x >= 0.5) ? 1 : 0;
#endif  // VR
		return 0;
	}

	/**
	* @brief Converts UV coordinates from the range [0, 1] to normalized screen space [-1, 1].
	*
	* This function takes texture coordinates and transforms them into a normalized
	* coordinate system centered at the origin. This is useful for various graphical
	* calculations, especially in shaders that require symmetry around the center.
	*
	* @param uv The input UV coordinates in the range [0, 1].
	* @return float2 Normalized screen space coordinates in the range [-1, 1].
	*/
	float2 ConvertUVToNormalizedScreenSpace(float2 uv)
	{
		float2 normalizedCoord;
		normalizedCoord.x = 2.0 * (-0.5 + abs(2.0 * (uv.x - 0.5)));  // Convert UV.x
		normalizedCoord.y = 2.0 * uv.y - 1.0;                        // Convert UV.y
		return normalizedCoord;
	}

#if defined(PSHADER) || defined(FRAMEBUFFER)
	// These functions require the framebuffer which is typically provided with the PSHADER
	/**
	Gets the eyeIndex for PSHADER
	@returns eyeIndex (0 left, 1 right)
	*/
	uint GetEyeIndexPS(float4 position, float4 offset = 0.0.xxxx)
	{
#	if !defined(VR)
		uint eyeIndex = 0;
#	else
		float4 stereoUV;
		stereoUV.xy = position.xy * offset.xy + offset.zw;
		stereoUV.x = FrameBuffer::DynamicResolutionParams2.x * stereoUV.x;
		stereoUV.x = (stereoUV.x >= 0.5);
		uint eyeIndex = (uint)(((int)((uint)StereoEnabled)) * (int)stereoUV.x);
#	endif
		return eyeIndex;
	}

	/**
	* @brief Checks if the color is non zero by testing if the color is greater than 0 by epsilon.
	*
	* This function check is a color is non black. It uses a small epsilon value to allow for
	* floating point imprecision.
	*
	* For screen-space reflection (SSR), this acts as a mask and checks for an invalid reflection by
	* checking if the reflection color is essentially black (close to zero).
	*
	* @param[in] ssrColor The color to check.
	* @param[in] epsilon Small tolerance value used to determine if the color is close to zero.
	* @return True if color is non zero, otherwise false.
	*/
	bool IsNonZeroColor(float4 ssrColor, float epsilon = 0.001)
	{
		return dot(ssrColor.xyz, ssrColor.xyz) > epsilon * epsilon;
	}

#	ifdef VR
	/**
	* @brief Converts mono UV coordinates from one eye to the corresponding mono UV coordinates of the other eye.
	*
	* This function is used to transition UV coordinates from one eye's perspective to the other eye in a stereo rendering setup.
	* It operates by converting the mono UV to clip space, transforming it into world space, and then reprojecting it
	* into the other eye's clip space before converting back to UV coordinates. It supports dynamic resolution adjustments
	* and applies eye offset adjustments for correct stereo separation.
	*
	* The function considers the aspect of VR by modifying the NDC to view space conversion based on the stereo setup,
	* ensuring accurate rendering across both eyes.
	*
	* @param[in] monoUV The UV coordinates and depth value (Z component) for the current eye, in the range [0,1].
	* @param[in] eyeIndex Index of the source/current eye (0 for left, 1 for right).
	* @param[in] dynamicres Optional flag indicating whether dynamic resolution is applied. Default is false.
	* @return UV coordinates adjusted to the other eye, with depth.
	*/
	float3 ConvertMonoUVToOtherEye(float3 monoUV, uint eyeIndex, bool dynamicres = false)
	{
		// Convert from dynamic res to true UV space if necessary
		if (dynamicres)
			monoUV.xy *= FrameBuffer::DynamicResolutionParams2.xy;

		// Convert UV to Clip Space
		float4 clipPos = float4(monoUV.xy * float2(2, -2) - float2(1, -1), monoUV.z, 1);

		// Convert Clip Space to World Space for the current eye
		float4 worldPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], clipPos);
		worldPos /= worldPos.w;

		// Apply eye offset adjustment in world space
		worldPos.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[1 - eyeIndex].xyz;

		// Convert World Space to Clip Space for the other eye
		float4 clipPosOtherEye = mul(FrameBuffer::CameraViewProj[1 - eyeIndex], worldPos);
		clipPosOtherEye /= clipPosOtherEye.w;

		// Convert Clip Space to UV (Y is flipped: clip +1 = top, UV 0 = top)
		float3 monoUVOtherEye = float3(clipPosOtherEye.xy * float2(0.5f, -0.5f) + 0.5f, clipPosOtherEye.z);

		// Convert back to dynamic res space if necessary
		if (dynamicres)
			monoUVOtherEye.xy *= FrameBuffer::DynamicResolutionParams1.xy;

		return monoUVOtherEye;
	}
#	endif  // VR

	/**
	* @brief Resolves a mono UV to the eye that can see it, crossing to the other eye if needed.
	*
	* When a screen-space ray or sample position leaves the current eye's screen bounds,
	* this function tries to find the corresponding location in the other eye via
	* ConvertMonoUVToOtherEye.  On flat (non-VR) this is a no-op: sampleUV and
	* sampleEyeIndex are set to the input values unchanged.
	*
	* Based on concepts from https://cuteloong.github.io/publications/scssr24/
	* Wu, X., Xu, Y., & Wang, L. (2024). Stereo-consistent Screen Space Reflection. Computer Graphics Forum, 43(4).
	*
	* @param[in]    monoUV          Mono UV coordinates with depth in Z, [0-1]. Must not be dynamic resolution adjusted.
	* @param[in]    eyeIndex        Index of the originating eye (0 or 1).
	* @param[out]   sampleUV        Mono UV that should be used for sampling (may be in the other eye).
	* @param[out]   sampleEyeIndex  Eye index that owns sampleUV.
	*/
	void ResolveMonoUVForEye(float3 monoUV, uint eyeIndex, out float2 sampleUV, out uint sampleEyeIndex)
	{
		sampleUV = monoUV.xy;
		sampleEyeIndex = eyeIndex;
#		ifdef VR
		if (FrameBuffer::IsOutsideFrame(monoUV.xy, false)) {
			float3 otherEyeUV = ConvertMonoUVToOtherEye(monoUV, eyeIndex);
			if (!FrameBuffer::IsOutsideFrame(otherEyeUV.xy, false)) {
				sampleUV = otherEyeUV.xy;
				sampleEyeIndex = 1 - eyeIndex;
			}
		}
#		endif
	}

#	ifdef VR
	/**
	* @brief Adjusts UV coordinates for VR stereo rendering when transitioning between eyes or handling boundary conditions.
	*
	* This function is used in raymarching to check the next UV coordinate. It checks if the current UV coordinates are outside
	* the frame. If so, it transitions the UV coordinates to the other eye and adjusts them if they are within the frame of the other eye.
	* If the UV coordinates are outside the frame of both eyes, it returns the adjusted UV coordinates for the current eye.
	*
	* The function ensures that the UV coordinates are correctly adjusted for stereo rendering, taking into account boundary conditions
	* and preserving accurate reflections.
	* Based on concepts from https://cuteloong.github.io/publications/scssr24/
	* Wu, X., Xu, Y., & Wang, L. (2024). Stereo-consistent Screen Space Reflection. Computer Graphics Forum, 43(4).
	*
	* We do not have a backface depth so we may be ray marching even though the ray is in an object.

	* @param[in] monoUV Current UV coordinates with depth information, [0-1]. Must not be dynamic resolution adjusted.
	* @param[in] eyeIndex Index of the current eye (0 or 1).
	* @param[out] fromOtherEye Boolean indicating if the result UV coordinates are from the other eye.
	*
	* @return Adjusted stereo UV coordinates for rendering, [0-1]. Must be dynamic resolution adjusted later.
	*/
	float3 ConvertStereoRayMarchUV(float3 monoUV, uint eyeIndex, out bool fromOtherEye)
	{
		float2 resolvedUV;
		uint resolvedEye;
		ResolveMonoUVForEye(monoUV, eyeIndex, resolvedUV, resolvedEye);
		fromOtherEye = (resolvedEye != eyeIndex);
		return ConvertToStereoUV(float3(resolvedUV, monoUV.z), resolvedEye);
	}

	/**
	* @brief Converts stereo UV coordinates from one eye to the corresponding stereo UV coordinates of the other eye.
	*
	* This function is used to transition UV coordinates from one eye's perspective to the other eye in a stereo rendering setup.
	* It works by converting the stereo UV to mono UV, then to clip space, transforming it into view space, and then reprojecting it into the other eye's
	* clip space before converting back to stereo UV coordinates. It also supports dynamic resolution.
	*
	* @param[in] stereoUV The UV coordinates and depth value (Z component) for the current eye, in the range [0,1].
	* @param[in] eyeIndex Index of the current eye (0 or 1).
	* @param[in] dynamicres Optional flag indicating whether dynamic resolution is applied. Default is false.
	* @return UV coordinates adjusted to the other eye, with depth.
	*/
	float3 ConvertStereoUVToOtherEyeStereoUV(float3 stereoUV, uint eyeIndex, bool dynamicres = false)
	{
		// Convert from dynamic res to true UV space
		if (dynamicres)
			stereoUV.xy *= FrameBuffer::DynamicResolutionParams2.xy;

		stereoUV.xy = ConvertFromStereoUV(stereoUV.xy, eyeIndex);
		stereoUV.xyz = ConvertMonoUVToOtherEye(stereoUV.xyz, eyeIndex);
		stereoUV.xy = ConvertToStereoUV(stereoUV.xy, 1 - eyeIndex);

		// Convert back to dynamic res space if necessary
		if (dynamicres)
			stereoUV.xy *= FrameBuffer::DynamicResolutionParams1.xy;
		return stereoUV;
	}

	/**
	* @brief Returns a smooth fade factor for UVs near the edge of the frame.
	*
	* This helps avoid abrupt transitions when one eye's SSGI is out of frame or occluded.
	* Fade width is tunable; 0.02 is 2% of the frame.
	*/
	float IsOutsideFrameFade(float2 uv, bool dynamicres = false)
	{
		float2 max = dynamicres ? FrameBuffer::DynamicResolutionParams1.xy : float2(1, 1);
		float2 min = float2(0, 0);
		float fadeWidth = 0.02;
		float edgeFade = 1.0;
		edgeFade *= smoothstep(min.x, min.x + fadeWidth, uv.x);
		edgeFade *= smoothstep(max.x, max.x - fadeWidth, uv.x);
		edgeFade *= smoothstep(min.y, min.y + fadeWidth, uv.y);
		edgeFade *= smoothstep(max.y, max.y - fadeWidth, uv.y);
		return edgeFade;
	}

	/**
	* @brief Blends color data from two eyes based on their UV coordinates and validity.
	*
	* This function checks the validity of the colors based on their UV coordinates and
	* alpha values. It blends the colors while ensuring proper handling of transparency.
	* If one eye sees the first person model (depth < VR_FP_Z) and the other sees world geometry (depth > VR_FP_Z),
	* the first person model's color is dropped from the blend to avoid outlines.
	*
	* @param uv1 UV coordinates for the first eye.
	* @param color1 Color from the first eye.
	* @param uv2 UV coordinates for the second eye.
	* @param color2 Color from the second eye.
	* @param dynamicres Whether the uvs have dynamic resolution applied
	* @return Blended color, including the maximum alpha from both inputs.
	*/
	float4 BlendEyeColors(
		float3 uv1,
		float4 color1,
		float3 uv2,
		float4 color2,
		bool dynamicres = false)
	{
		// Use smooth fade at edge for each eye
		float fade1 = IsOutsideFrameFade(uv1.xy, dynamicres);
		float fade2 = IsOutsideFrameFade(uv2.xy, dynamicres);

		// Stereo-consistent edge fade: use maximum fade so either eye can keep color if in bounds
		float edgeFade = max(fade1, fade2);

		// Occlusion-aware confidence based on depth difference
		float depthDiff = abs(uv1.z - uv2.z);
		float confidence = 1.0 - smoothstep(0.01, 0.05, depthDiff);

		// Soft first person model mask: fade out FP model near threshold
		float fp_fade1 = 1.0 - smoothstep(VR_FP_Z - 1.0, VR_FP_Z + 1.0, uv1.z); // fades from 1 to 0 as depth crosses VR_FP_Z
		float fp_fade2 = 1.0 - smoothstep(VR_FP_Z - 1.0, VR_FP_Z + 1.0, uv2.z);

		// If one eye is world and the other is FP, fade out FP smoothly
		bool eye1_is_fp = uv1.z < VR_FP_Z;
		bool eye2_is_fp = uv2.z < VR_FP_Z;
		bool eyes_disagree = eye1_is_fp != eye2_is_fp;
		if (eyes_disagree) {
			if (eye1_is_fp) fade1 *= fp_fade1;
			if (eye2_is_fp) fade2 *= fp_fade2;
		}

		fade1 *= confidence * edgeFade;
		fade2 *= confidence * edgeFade;

		float totalFade = fade1 + fade2 + 1e-5;
		float4 blendedColor = (color1 * fade1 + color2 * fade2) / totalFade;
		blendedColor.a = max(color1.a * fade1, color2.a * fade2);
		return blendedColor;
	}

	float4 BlendEyeColors(float2 uv1, float4 color1, float2 uv2, float4 color2, bool dynamicres = false)
	{
		return BlendEyeColors(float3(uv1, 0), color1, float3(uv2, 0), color2, dynamicres);
	}
#	endif  // VR
#endif      // PSHADER

#ifdef VSHADER
	struct VR_OUTPUT
	{
		float4 VRPosition;
		float ClipDistance;
		float CullDistance;
	};

	/**
	Gets the eyeIndex for VSHADER
	@returns eyeIndex (0 left, 1 right)
	*/
	uint GetEyeIndexVS(uint instanceID = 0)
	{
#	ifdef VR
		return StereoEnabled * (instanceID & 1);
#	endif  // VR
		return 0;
	}

	/**
	Gets VR Output
	@param clipPos clipPosition. Typically the VSHADER position at SV_POSITION0
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@returns VR_OUTPUT with VR values
	*/
	VR_OUTPUT GetVRVSOutput(float4 clipPos, uint a_eyeIndex = 0)
	{
		VR_OUTPUT vsout = {
			0.0.xxxx,  // VRPosition
			0.0f,      // ClipDistance
			0.0f       // CullDistance
		};

#	ifdef VR
		bool isStereoEnabled = (StereoEnabled != 0);
		float2 clipEdges;

		if (isStereoEnabled) {
			clipEdges.x = dot(clipPos, EyeClipEdge[a_eyeIndex]);
			clipEdges.y = clipEdges.x;  // Both use the same calculation
		} else {
			clipEdges = float2(1.0f, 1.0f);
		}

		float stereoAdjustment = 2.0f - StereoEnabled;
		float eyeOffset = dot(EyeOffsetScale, Math::IdentityMatrix[a_eyeIndex].xy);

		float xPositionOffset = eyeOffset * clipPos.w * (isStereoEnabled ? 1.0f : 0.0f);
		float xPositionBase = stereoAdjustment * clipPos.x;

		vsout.VRPosition.x = xPositionBase * 0.5f + xPositionOffset;
		vsout.VRPosition.y = clipPos.y;
		vsout.VRPosition.z = clipPos.z;
		vsout.VRPosition.w = clipPos.w;

		vsout.ClipDistance = clipEdges.y;
		vsout.CullDistance = clipEdges.x;
#	endif  // VR
		return vsout;
	}
#endif

}
#endif  //__VR_DEPENDENCY_HLSL__