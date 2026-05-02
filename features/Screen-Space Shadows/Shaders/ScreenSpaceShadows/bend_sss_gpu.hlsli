// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// If you have feedback, or found this code useful, we'd love to hear from you.
// https://www.bendstudio.com
// https://www.twitter.com/bendstudio
//
// We are *always* looking for talented graphics and technical programmers!
// https://www.bendstudio.com/careers

// Common screen space shadow projection code (GPU):
//--------------------------------------------------------------

// The main shadow generation function is WriteScreenSpaceShadow(), it will read a depth texture, and write to a shadow texture
// This code is setup to target DX12 DXC shader compiler, but has also been tested on PS5 with appropriate API remapping.
// It can compile to DX11, but requires some modifications (e.g., early-out's use of wave intrinsics is not supported in DX11).
// Note; you can customize the 'EarlyOutPixel' function to perform custom early-out logic to optimize this shader.

#define WAVE_SIZE 64  // Wavefront size of the compute shader running this code.                                                                                                                      \

//#if defined(__HLSL_VERSION) || defined(__hlsl_dx_compiler)

#define USE_HALF_PIXEL_OFFSET 1  // Apply a 0.5 texel offset when sampling a texture. Toggle this macro if the output shadow has odd, regular grid-like artefacts.

// HLSL enforces that a pixel offset in a Sample() call must be a compile time constant, which isn't always required - and in some cases can give a small perf boost if used.
#define USE_UV_PIXEL_BIAS 1  // Use Sample(uv + bias) instead of Sample(uv, bias)

//#endif

// This is the list of runtime properties to pass to the shader
// Wherever possible, it is highly recommended to have these values be compile-time constants
struct DispatchParameters
{
	// Visual configuration:
	// These values will require manual tuning.
	// All shadow computation is performed in non-linear depth space (not in world space), so tuned value choices will depend on scene depth distribution (as determined by the Projection Matrix setup).

	half SurfaceThickness;  // This is the assumed thickness of each pixel for shadow-casting, measured as a percentage of the difference in non-linear depth between the sample and FarDepthValue.
							// Recommended starting value: 0.005 (0.5%)

	half BilinearThreshold;  // Percentage threshold for determining if the difference between two depth values represents an edge, and should not perform interpolation.
							 // To tune this value, set 'DebugOutputEdgeMask' to true to visualize where edges are being detected.
							 // Recommended starting value: 0.02 (2%)

	half ShadowContrast;  // A contrast boost is applied to the transition in/out of shadow.
						  // Recommended starting value: 2 or 4. Values >= 1 are valid.

	float2 DynamicRes;

	bool IgnoreEdgePixels;  // If an edge is detected, the edge pixel will not contribute to the shadow.
							// If a very flat surface is being lit and rendered at an grazing angles, the edge detect may incorrectly detect multiple 'edge' pixels along that flat surface.
							// In these cases, the grazing angle of the light may subsequently produce aliasing artefacts in the shadow where these incorrect edges were detected.
							// Setting this value to true would mean that those pixels would not cast a shadow, however it can also thin out otherwise valid shadows, especially on foliage edges.
							// Recommended starting value: false, unless typical scenes have numerous large flat surfaces, in which case true.

	bool UsePrecisionOffset;  // A small offset is applied to account for an imprecise depth buffer (recommend off)

	bool BilinearSamplingOffsetMode;  // There are two modes to compute bilinear samples for shadow depth:
									  // true = sampling points for pixels are offset to the wavefront shared ray, shadow depths and starting depths are the same. Can project more jagged/aliased shadow lines in some cases.
									  // false = sampling points for pixels are not offset and start from pixel centers. Shadow depths are biased based on depth gradient across the current pixel bilinear sample. Has more issues in back-face / grazing areas.
									  // Both modes have subtle visual differences, which may / may not exaggerate depth buffer aliasing that gets projected in to the shadow.
									  // Evaluating the visual difference between each mode is recommended, then hard-coding the mode used to optimize the shader.
									  // Recommended starting value: false

	// Debug views
	bool DebugOutputEdgeMask;     // Use this to visualize edges, for tuning the 'BilinearThreshold' value.
	bool DebugOutputThreadIndex;  // Debug output to visualize layout of compute threads
	bool DebugOutputWaveIndex;    // Debug output to visualize layout of compute wavefronts, useful to sanity check the Light Coordinate is being computed correctly.

	// Culling / Early out:
	//half2 DepthBounds;					// Depth Bounds (min, max) for the on-screen volume of the light. Typically (0,1) for directional lights. Only used when 'UseEarlyOut' is true.

	//bool UseEarlyOut;					// Set to true to early-out when depth values are not within [DepthBounds] - otherwise DepthBounds is unused
	// [Optionally customize the 'EarlyOutPixel()' function to perform your own early-out logic, e.g. skipping pixels that a shadow map indicates are already fully occluded]
	// This can dramatically reduce cost when only a small portion of the pixels need a shadow term (e.g., cull out sky pixels), however it does have some overhead (~15%) in worst-case where nothing early-outs
	// Note; Early-out is most efficient when WAVE_SIZE matches the hardware wavefront size - otherwise cross wave communication is required.

	// Set sensible starting tuning values
	void SetDefaults()
	{
		SurfaceThickness = 0.005;
		BilinearThreshold = 0.02;
		ShadowContrast = 4;
		IgnoreEdgePixels = false;
		UsePrecisionOffset = false;
		BilinearSamplingOffsetMode = false;
		DebugOutputEdgeMask = false;
		DebugOutputThreadIndex = false;
		DebugOutputWaveIndex = false;
	}

	// Runtime data returned from BuildDispatchList():
	half4 LightCoordinate;  // Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
	int2 WaveOffset;        // Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

	// Renderer Specific Values:
	half FarDepthValue;   // Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
	half NearDepthValue;  // Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

	// Sampling data:
	half2 InvDepthTextureSize;  // Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
								// If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
								// The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

	// TERRAIN_BLENDING ON  -> bound to TerrainBlending::blendedDepthTexture (R32_FLOAT) — must NOT be unorm.
	// TERRAIN_BLENDING OFF -> bound to game's kPOST_ZPREPASS_COPY (R24_UNORM_X8_TYPELESS) — unorm.
#if defined(TERRAIN_BLENDING)
	Texture2D<float> DepthTexture;  // Depth Buffer Texture (rasterized non-linear depth, R32_FLOAT)
#else
	Texture2D<unorm float> DepthTexture;  // Depth Buffer Texture (rasterized non-linear depth, R24_UNORM_X8_TYPELESS)
#endif
	RWTexture2D<unorm float> OutputTexture;  // Output screen-space shadow buffer (typically single-channel, 8bit)

	SamplerState PointBorderSampler;  // A point sampler, with Wrap Mode set to Clamp-To-Border-Color (D3D12_TEXTURE_ADDRESS_MODE_BORDER), and Border Color set to "FarDepthValue" (typically zero), or some other far-depth value out of DepthBounds.
									  // If you have issues where invalid shadows are appearing from off-screen, it is likely that this sampler is not correctly setup
};

// Forward declare:
// Generate the shadow
//	Call this function from a compute shader with thread dimensions: numthreads[WAVE_SIZE, 1, 1]
//
//	(int3)	inGroupID:			Compute shader group id register (SV_GroupID)
//	(int)	inGroupThreadId:	Compute shader group thread id register (SV_GroupThreadID)
void WriteScreenSpaceShadow(struct DispatchParameters inParameters, int3 inGroupID, int inGroupThreadID);

// Gets the start pixel coordinates for the pixels in the wavefront
// Also returns the delta to get to the next pixel after WAVE_COUNT pixels along the ray
static void ComputeWavefrontExtents(DispatchParameters inParameters, int3 inGroupID, uint inGroupThreadID, out half2 outDeltaXY, out half2 outPixelXY, out half outPixelDistance, out bool outMajorAxisX)
{
	int2 xy = inGroupID.yz * WAVE_SIZE + inParameters.WaveOffset.xy;

	//integer light position / fractional component
	half2 light_xy = floor(inParameters.LightCoordinate.xy) + 0.5;
	half2 light_xy_fraction = inParameters.LightCoordinate.xy - light_xy;
	bool reverse_direction = inParameters.LightCoordinate.w > 0.0f;

	int2 sign_xy = sign(xy);
	bool horizontal = abs(xy.x + sign_xy.y) < abs(xy.y - sign_xy.x);

	int2 axis;
	axis.x = horizontal ? (+sign_xy.y) : (0);
	axis.y = horizontal ? (0) : (-sign_xy.x);

	// Apply wave offset
	xy = axis * (int)inGroupID.x + xy;
	half2 xy_f = (half2)xy;

	// For interpolation to the light center, we only really care about the larger of the two axis
	bool x_axis_major = abs(xy_f.x) > abs(xy_f.y);
	half major_axis = x_axis_major ? xy_f.x : xy_f.y;

	half major_axis_start = abs(major_axis);
	half major_axis_end = abs(major_axis) - (half)WAVE_SIZE;

	half ma_light_frac = x_axis_major ? light_xy_fraction.x : light_xy_fraction.y;
	ma_light_frac = major_axis > 0 ? -ma_light_frac : ma_light_frac;

	// back in to screen direction
	half2 start_xy = xy_f + light_xy;

	// For the very inner most ring, we need to interpolate to a pixel centered UV, so the UV->pixel rounding doesn't skip output pixels
	half2 end_xy = lerp(inParameters.LightCoordinate.xy, start_xy, (major_axis_end + ma_light_frac) / (major_axis_start + ma_light_frac));

	// The major axis should be a round number
	half2 xy_delta = (start_xy - end_xy);

	// Inverse the read order when reverse direction is true
	half thread_step = (half)(inGroupThreadID ^ (reverse_direction ? 0 : (WAVE_SIZE - 1)));

	half2 pixel_xy = lerp(start_xy, end_xy, thread_step / (half)WAVE_SIZE);
	half pixel_distance = major_axis_start - thread_step + ma_light_frac;

	outPixelXY = pixel_xy;
	outPixelDistance = pixel_distance;
	outDeltaXY = xy_delta;
	outMajorAxisX = x_axis_major;
}

// Number of bilinear sample reads performed per-thread
#define READ_COUNT (SAMPLE_COUNT / WAVE_SIZE + 2)

// Common shared data
groupshared half DepthData[READ_COUNT * WAVE_SIZE];

// Generate the shadow
//	Call this function from a compute shader with thread dimensions: numthreads[WAVE_SIZE, 1, 1]
//
//	(int3)	inGroupID:			Compute shader group id register (SV_GroupID)
//	(int)	inGroupThreadId:	Compute shader group thread id register (SV_GroupThreadID)
void WriteScreenSpaceShadow(DispatchParameters inParameters, int3 inGroupID, int inGroupThreadID)
{
	half2 xy_delta;
	half2 pixel_xy;
	half pixel_distance;
	bool x_axis_major;  // major axis is x axis? abs(xy_delta.x) > abs(xy_delta.y).

	ComputeWavefrontExtents(inParameters, (int3)inGroupID, inGroupThreadID.x, xy_delta, pixel_xy, pixel_distance, x_axis_major);

	// Read in the depth values
	half sampling_depth[READ_COUNT];
	half shadowing_depth[READ_COUNT];
	half depth_thickness_scale[READ_COUNT];
	half sample_distance[READ_COUNT];

	const half direction = -inParameters.LightCoordinate.w;
	const half z_sign = inParameters.NearDepthValue > inParameters.FarDepthValue ? -1 : +1;

	int i;
	bool is_edge = false;
	bool skip_pixel = false;

#if defined(RIGHT)
	pixel_xy.x += 1.0 / inParameters.InvDepthTextureSize.x;
#endif

	half2 write_xy = floor(pixel_xy);

	[unroll] for (i = 0; i < READ_COUNT; i++)
	{
		// We sample depth twice per pixel per sample, and interpolate with an edge detect filter
		// Interpolation should only occur on the minor axis of the ray - major axis coordinates should be at pixel centers
		half2 read_xy = floor(pixel_xy);

		half minor_axis = x_axis_major ? pixel_xy.y : pixel_xy.x;

		// If a pixel has been detected as an edge, then optionally (inParameters.IgnoreEdgePixels) don't include it in the shadow
		const half edge_skip = 1e20;  // if edge skipping is enabled, apply an extreme value/blend on edge samples to push the value out of range

		half2 depths;
		half bilinear = frac(minor_axis) - 0.5;

#if USE_HALF_PIXEL_OFFSET
		read_xy += 0.5;
#endif

		half bias = bilinear > 0 ? 1 : -1;
		half2 offset_xy = half2(x_axis_major ? 0 : bias, x_axis_major ? bias : 0);

		// HLSL enforces that a pixel offset is a compile-time constant, which isn't strictly required (and can sometimes be a bit faster)
		// So this fallback will use a manual uv offset instead
		// Apply DynamicRes after offset_xy addition so the bilinear neighbour samples exactly 1 texel away.
		half2 coord = read_xy * inParameters.InvDepthTextureSize * inParameters.DynamicRes;
		half2 coord_with_offset = (read_xy + offset_xy) * inParameters.InvDepthTextureSize * inParameters.DynamicRes;

#if defined(VR)
		// VR side-by-side: halve x to map stereo pixel coords to texture UV.
		coord *= half2(0.5, 1.0);
		coord_with_offset *= half2(0.5, 1.0);

#	if defined(RIGHT)
		// Right eye: valid UV range is [0.5*DynRes.x, DynRes.x]
		bool coord_out_of_eye = coord.x < 0.5 * inParameters.DynamicRes.x;
		bool coord_offset_out_of_eye = coord_with_offset.x < 0.5 * inParameters.DynamicRes.x;
#	else
		// Left eye: valid UV range is [0.0, 0.5*DynRes.x)
		bool coord_out_of_eye = coord.x >= 0.5 * inParameters.DynamicRes.x;
		bool coord_offset_out_of_eye = coord_with_offset.x >= 0.5 * inParameters.DynamicRes.x;
#	endif

		// Clamp cross-eye depth reads to FarDepthValue (1.0) so rays near the SBS center
		// seam see no occluder at the boundary. Shadow weakens by ~1 pixel at the seam but
		// stays temporally stable across camera movement.
		depths.x = coord_out_of_eye ? 1.0 : inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, coord, 0);
		depths.y = coord_offset_out_of_eye ? 1.0 : inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, coord_with_offset, 0);

		// HMD mask: depth==0 is outside the visible lens area. Remap to FarDepthValue so
		// mask pixels do not cast false shadows.
		depths.x = lerp(depths.x, 1.0, (float)(depths.x == 0));  // Stencil area
		depths.y = lerp(depths.y, 1.0, (float)(depths.y == 0));  // Stencil area
#else
		depths.x = inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, coord, 0);
		depths.y = inParameters.DepthTexture.SampleLevel(inParameters.PointBorderSampler, coord_with_offset, 0);
#endif

		// Depth thresholds (bilinear/shadow thickness) are based on a fractional ratio of the difference between sampled depth and the far clip depth
		static const half kDepthThicknessFloor = 1e-4h;  // Prevents division by zero in depth_scale when depth is at the far clip plane
		depth_thickness_scale[i] = max(abs(inParameters.FarDepthValue - depths.x), kDepthThicknessFloor);

		// If depth variance is more than a specific threshold, then just use point filtering
		bool use_point_filter = abs(depths.x - depths.y) > depth_thickness_scale[i] * inParameters.BilinearThreshold;

		// Store for debug output when inParameters.DebugOutputEdgeMask is true
		if (i == 0)
			is_edge = use_point_filter;

		// The pixel starts sampling at this depth
		sampling_depth[i] = depths.x;

		half edge_depth = inParameters.IgnoreEdgePixels ? edge_skip : depths.x;
		// Any sample in this wavefront is possibly interpolated towards the bilinear sample
		// So use should use a shadowing depth that is further away, based on the difference between the two samples
		half shadow_depth = depths.x + abs(depths.x - depths.y) * z_sign;

		// Shadows cast from this depth
		shadowing_depth[i] = use_point_filter ? edge_depth : shadow_depth;

		// Store for later
		sample_distance[i] = pixel_distance + (WAVE_SIZE * i) * direction;

		// Iterate to the next pixel along the ray. This will be WAVE_SIZE pixels along the ray...
		pixel_xy += xy_delta * direction;
	}

	// Write the shadow depths to LDS
	[unroll] for (i = 0; i < READ_COUNT; i++)
	{
		// Perspective correct the shadowing depth, in this space, all light rays are parallel
		half stored_depth = (shadowing_depth[i] - inParameters.LightCoordinate.z) / sample_distance[i];

		if (i != 0) {
			// For pixels within sampling distance of the light, it is possible that sampling will
			// overshoot the light coordinate for extended reads. We want to ignore these samples
			stored_depth = sample_distance[i] > 0 ? stored_depth : 1e10;
		}

		// Store the depth values in groupshared
		int idx = (i * WAVE_SIZE) + inGroupThreadID.x;
		DepthData[idx] = stored_depth;
	}

	// Sync wavefronts now groupshared DepthData is written
	GroupMemoryBarrierWithGroupSync();

#if defined(VR)
	// Check if the pixel we're writing to is on the correct eye side
	half writeX = write_xy.x * inParameters.InvDepthTextureSize.x;

#	if defined(RIGHT)
	if (writeX < 0.0)
		return;
#	else
	if (writeX > 1.0)
		return;
#	endif
#endif

	half start_depth = sampling_depth[0];

	if (start_depth == 0.0 || start_depth == 1.0)
		return;

	// lerp away from far depth by a tiny fraction?
	if (inParameters.UsePrecisionOffset)
		start_depth = lerp(start_depth, inParameters.FarDepthValue, -1.0 / 0xFFFF);

	// perspective correct the depth
	start_depth = (start_depth - inParameters.LightCoordinate.z) / sample_distance[0];

	// Start by reading the next value
	int sample_index = inGroupThreadID.x + 1;

	half4 shadow_value = 1;

	// This is the inverse of how large the shadowing window is for the projected sample data.
	// All values in the LDS sample list are scaled by 1.0 / sample_distance, such that all light directions become parallel.
	// The multiply by sample_distance[0] here is to compensate for the projection divide in the data.
	// The 1.0 / inParameters.SurfaceThickness is to adjust user selected thickness. So a 0.5% thickness will scale depth values from [0,1] to [0,200]. The shadow window is always 1 wide.
	// 1.0 / depth_thickness_scale[0] is because SurfaceThickness is percentage of remaining depth between the sample and the far clip - not a percentage of the full depth range.
	// The min() function is to make sure the window is a minimum width when very close to the light. The +direction term will bias the result so the pixel at the very center of the light is either fully lit or shadowed
	half depth_scale = min(sample_distance[0] + direction, 1.0 / inParameters.SurfaceThickness) * sample_distance[0] / depth_thickness_scale[0];

	start_depth = start_depth * depth_scale - z_sign;

	[unroll] for (i = 0; i < SAMPLE_COUNT; i++)
	{
		half depth_delta = abs(start_depth - DepthData[sample_index + i] * depth_scale);

		// By using 4 values, the average shadow can be taken, which can help soften single-pixel shadows.
		shadow_value[i & 3] = min(shadow_value[i & 3], depth_delta);
	}

	// Apply the contrast value.
	// A value of 0 indicates a sample was exactly matched to the reference depth (and the result is fully shadowed)
	// We want some boost to this range, so samples don't have to exactly match to produce a full shadow.
	shadow_value = saturate(shadow_value * (inParameters.ShadowContrast) + (1 - inParameters.ShadowContrast));

	half result = 0;

	// Take the average of 4 samples, this is useful to reduces aliasing noise in the source depth, especially with long shadows.
	result = dot(shadow_value, 0.25);

	// Asking the GPU to write scattered single-byte pixels isn't great,
	// But thankfully the latency is hidden by all the work we're doing...
	inParameters.OutputTexture[(int2)write_xy] = result;
}