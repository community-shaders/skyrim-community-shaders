/**
 * Copyright (C) 2012 Jorge Jimenez (jorge@iryoku.com)
 * Copyright (C) 2012 Diego Gutierrez (diegog@unizar.es)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the following disclaimer
 *       in the documentation and/or other materials provided with the
 *       distribution:
 *
 *       "Uses Separable SSS. Copyright (C) 2012 by Jorge Jimenez and Diego
 *        Gutierrez."
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the copyright holders.
 */

/**
 *                  _______      _______      _______      _______
 *                 /       |    /       |    /       |    /       |
 *                |   (----    |   (----    |   (----    |   (----
 *                 \   \        \   \        \   \        \   \
 *              ----)   |    ----)   |    ----)   |    ----)   |
 *             |_______/    |_______/    |_______/    |_______/
 *
 *        S E P A R A B L E   S U B S U R F A C E   S C A T T E R I N G
 *
 *                           http://www.iryoku.com/
 *
 * Hi, thanks for your interest in Separable SSS!
 *
 * It's a simple shader composed of two components:
 *
 * 1) A transmittance function, 'SSSSTransmittance', which allows to calculate
 *    light transmission in thin slabs, useful for ears and nostrils. It should
 *    be applied during the main rendering pass as follows:
 *
 *        float3 t = albedo.rgb * lights[i].color * attenuation * spot;
 *        color.rgb += t * SSSSTransmittance(...)
 *
 *    (See 'Main.fx' for more details).
 *
 * 2) A simple two-pass reflectance post-processing shader, 'SSSSBlur*', which
 *    softens the skin appearance. It should be applied as a regular
 *    post-processing effect like bloom (the usual framebuffer ping-ponging):
 *
 *    a) The first pass (horizontal) must be invoked by taking the final color
 *       framebuffer as input, and storing the results into a temporal
 *       framebuffer.
 *    b) The second pass (vertical) must be invoked by taking the temporal
 *       framebuffer as input, and storing the results into the original final
 *       color framebuffer.
 *
 *    Note that This SSS filter should be applied *before* tonemapping.
 *
 * Before including SeparableSSS.h you'll have to setup the target. The
 * following targets are available:
 *         SMAA_HLSL_3
 *         SMAA_HLSL_4
 *         SMAA_GLSL_3
 *
 * For more information of what's under the hood, you can check the following
 * URLs (but take into account that the shader has evolved a little bit since
 * these publications):
 *
 * 1) Reflectance: http://www.iryoku.com/sssss/
 * 2) Transmittance: http://www.iryoku.com/translucency/
 *
 * If you've got any doubts, just contact us!
 */

//-----------------------------------------------------------------------------

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

float4 SSSSBlurCS(
	float2 texcoord,
	float2 dir,
	float sssAmount,
	bool humanProfile)
{
	// texcoord is in DR-space UVs (coming from DTid / full buffer dim), so it already
	// addresses the rendered sub-rectangle of the texture. Non-DR UVs are derived for
	// eye-half detection during clamping.
	float2 texcoordNonDR = texcoord * FrameBuffer::DynamicResolutionParams2.xy;

	// Input is already linear and albedo-free from the pre-pass
	float4 colorM = ColorTexture.SampleLevel(PointSampler, texcoord, 0);

	if (sssAmount == 0)
		return colorM;

	// Fetch linear depth of current pixel:
	float depthM = DepthTexture.SampleLevel(PointSampler, texcoord, 0).r;
	depthM = SharedData::GetScreenDepth(depthM);

	float2 profile = humanProfile ? HumanProfile.xy : BaseProfile.xy;
	uint kernelOffset = humanProfile ? SSSS_N_SAMPLES : 0;

	// Accumulate center sample, multiplying it with its gaussian weight:
	float4 colorBlurred = colorM;
	colorBlurred.rgb *= Kernels[kernelOffset].rgb;

	// World-space width
	float distanceToProjectionWindow = 1.0 / tan(0.5 * radians(SSSS_FOVY));
	float scale = distanceToProjectionWindow / depthM;

	// Calculate the final step to fetch the surrounding pixels:
	float2 finalStep = scale * dir;
	finalStep *= sssAmount;
	finalStep *= profile.x;  // Modulate it using the profile
	// Scale the step into DR-UV space so blur width in rendered pixels stays consistent.
	finalStep *= FrameBuffer::DynamicResolutionParams1.xy;

	// Per-pixel rotation to break separable axis-aligned banding
	float jitter = Random::InterleavedGradientNoise(texcoord * SharedData::BufferDim.xy, SharedData::FrameCount) * Math::TAU;
	float2x2 rotationMatrix = float2x2(cos(jitter), sin(jitter), -sin(jitter), cos(jitter));

	// Accumulate the other samples:
	for (uint i = kernelOffset + 1; i < kernelOffset + SSSS_N_SAMPLES; i++) {
		float2 offset = Kernels[i].a * finalStep;

		// Apply randomized rotation
		offset = mul(offset, rotationMatrix);

		float2 sampleCoord = texcoord + offset;

		// Clamp to the DR-rendered region (per-eye in VR) to avoid sampling outside it.
		sampleCoord = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(sampleCoord, texcoordNonDR);

		float3 color = ColorTexture.SampleLevel(PointSampler, sampleCoord, 0).rgb;

		float depth = DepthTexture.SampleLevel(PointSampler, sampleCoord, 0).r;
		depth = SharedData::GetScreenDepth(depth);

		// If the difference in depth is huge, we lerp color back to "colorM":
		float s = saturate(profile.y * distanceToProjectionWindow * abs(depthM - depth));
		color = lerp(color, colorM.rgb, s * s);

		// Accumulate:
		colorBlurred.rgb += Kernels[i].rgb * color.rgb;
	}

	return colorBlurred;
}

//-----------------------------------------------------------------------------
