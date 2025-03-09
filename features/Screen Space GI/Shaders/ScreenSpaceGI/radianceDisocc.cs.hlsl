#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/VR.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<half4> srcDiffuse : register(t0);
Texture2D<half> srcCurrDepth : register(t1);
Texture2D<half4> srcCurrNormal : register(t2);
Texture2D<half3> srcPrevGeo : register(t3);  // maybe half-res
Texture2D<float4> srcMotionVec : register(t4);
Texture2D<half3> srcPrevAmbient : register(t5);
Texture2D<unorm float> srcAccumFrames : register(t6);  // maybe half-res
Texture2D<half> srcPrevAo : register(t7);              // maybe half-res
Texture2D<half4> srcPrevIlY : register(t8);            // maybe half-res
Texture2D<half2> srcPrevIlCoCg : register(t9);         // maybe half-res
Texture2D<half4> srcPrevGISpecular : register(t10);    // maybe half-res

RWTexture2D<float3> outRadianceDisocc : register(u0);
RWTexture2D<unorm float> outAccumFrames : register(u1);
RWTexture2D<float> outRemappedAo : register(u2);
RWTexture2D<float4> outRemappedIlY : register(u3);
RWTexture2D<float2> outRemappedIlCoCg : register(u4);
RWTexture2D<float4> outRemappedPrevGISpecular : register(u5);

#if (defined(GI) && defined(GI_BOUNCE)) || defined(TEMPORAL_DENOISER) || defined(HALF_RATE)
#	define REPROJECTION
#endif

void readHistory(
	uint eyeIndex, float curr_depth, float3 curr_pos, int2 pixCoord, float bilinear_weight,
	inout half prev_ao, inout half4 prev_y, inout half2 prev_co_cg, inout half3 prev_ambient, inout float accum_frames, inout half4 prev_gi_specular, inout float wsum)
{
	const float2 uv = (pixCoord + .5) * RCP_OUT_FRAME_DIM;
	const float2 screen_pos = Stereo::ConvertFromStereoUV(uv, eyeIndex);
	if (any(screen_pos < 0) || any(screen_pos > 1))
		return;

	const half3 prev_geo = srcPrevGeo[pixCoord];
	const float prev_depth = prev_geo.x;
	// const float3 prev_normal = GBuffer::DecodeNormal(prev_geo.yz);  // prev normal is already world
	float3 prev_pos = ScreenToViewPosition(screen_pos, prev_depth, eyeIndex);
	prev_pos = ViewToWorldPosition(prev_pos, PrevInvViewMat[eyeIndex]) + FrameBuffer::CameraPreviousPosAdjust[eyeIndex].xyz;

	float3 delta_pos = curr_pos - prev_pos;
	// float normal_prod = dot(curr_normal, prev_normal);

	const float movement_thres = curr_depth * DepthDisocclusion;

	bool depth_pass = dot(delta_pos, delta_pos) < movement_thres * movement_thres;
	// bool normal_pass = normal_prod * normal_prod > NormalDisocclusion;
	if (depth_pass) {
#if defined(GI) && defined(GI_BOUNCE)
		prev_ambient += FULLRES_LOAD(srcPrevAmbient, pixCoord, uv * FrameDim * RcpTexDim, samplerLinearClamp) * bilinear_weight;
#endif
#ifdef TEMPORAL_DENOISER
		prev_ao += srcPrevAo[pixCoord] * bilinear_weight;
		prev_y += srcPrevIlY[pixCoord] * bilinear_weight;
		prev_co_cg += srcPrevIlCoCg[pixCoord] * bilinear_weight;
		accum_frames += srcAccumFrames[pixCoord] * bilinear_weight;
#	ifdef GI_SPECULAR
		prev_gi_specular += srcPrevGISpecular[pixCoord] * bilinear_weight;
#	endif
#endif
		wsum += bilinear_weight;
	}
};

[numthreads(8, 8, 1)] void main(const uint2 pixCoord
								: SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	const float2 uv = (pixCoord + .5) * RCP_OUT_FRAME_DIM;
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	const float2 screen_pos = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float2 prev_screen_pos = screen_pos;
#ifdef REPROJECTION
	prev_screen_pos += FULLRES_LOAD(srcMotionVec, pixCoord, uv * frameScale, samplerLinearClamp).xy;
#endif
	float2 prev_uv = Stereo::ConvertToStereoUV(prev_screen_pos, eyeIndex);

	half3 prev_ambient = 0;
	half prev_ao = 0;
	half4 prev_y = 0;
	half2 prev_co_cg = 0;
	half4 prev_gi_specular = 0;
	float accum_frames = 0;
	float wsum = 0;

	const float curr_depth = READ_DEPTH(srcCurrDepth, pixCoord);

	if (curr_depth < FP_Z) {
		outRadianceDisocc[pixCoord] = half3(0, 0, 0);
		outAccumFrames[pixCoord] = 1.0 / 255.0;
		outRemappedIlY[pixCoord] = half4(0, 0, 0, 0);
		outRemappedIlCoCg[pixCoord] = half2(0, 0);
		return;
	}

#ifdef REPROJECTION
	if ((curr_depth <= DepthFadeRange.y) && !(any(prev_screen_pos < 0) || any(prev_screen_pos > 1))) {
		// float3 curr_normal = GBuffer::DecodeNormal(srcCurrNormal[pixCoord]);
		// curr_normal = ViewToWorldVector(curr_normal, FrameBuffer::CameraViewInverse[eyeIndex]);
		float3 curr_pos = ScreenToViewPosition(screen_pos, curr_depth, eyeIndex);
		curr_pos = ViewToWorldPosition(curr_pos, FrameBuffer::CameraViewInverse[eyeIndex]) + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

		float2 prev_px_coord = prev_uv * OUT_FRAME_DIM;
		int2 prev_px_lu = floor(prev_px_coord - 0.5);
		float2 bilinear_weights = prev_px_coord - 0.5 - prev_px_lu;

		readHistory(eyeIndex, curr_depth, curr_pos,
			prev_px_lu, (1 - bilinear_weights.x) * (1 - bilinear_weights.y),
			prev_ao, prev_y, prev_co_cg, prev_ambient, accum_frames, prev_gi_specular, wsum);
		readHistory(eyeIndex, curr_depth, curr_pos,
			prev_px_lu + int2(1, 0), bilinear_weights.x * (1 - bilinear_weights.y),
			prev_ao, prev_y, prev_co_cg, prev_ambient, accum_frames, prev_gi_specular, wsum);
		readHistory(eyeIndex, curr_depth, curr_pos,
			prev_px_lu + int2(0, 1), (1 - bilinear_weights.x) * bilinear_weights.y,
			prev_ao, prev_y, prev_co_cg, prev_ambient, accum_frames, prev_gi_specular, wsum);
		readHistory(eyeIndex, curr_depth, curr_pos,
			prev_px_lu + int2(1, 1), bilinear_weights.x * bilinear_weights.y,
			prev_ao, prev_y, prev_co_cg, prev_ambient, accum_frames, prev_gi_specular, wsum);

		if (wsum > 1e-2) {
			float rcpWsum = rcp(wsum + 1e-10);
#	ifdef TEMPORAL_DENOISER
			prev_ao *= rcpWsum;
			prev_y *= rcpWsum;
			prev_co_cg *= rcpWsum;
			accum_frames *= rcpWsum;
#		ifdef GI_SPECULAR
			prev_gi_specular *= rcpWsum;
#		endif
#	endif
#	if defined(GI) && defined(GI_BOUNCE)
			prev_ambient *= rcpWsum;
#	endif
		}
	}
#endif

	half3 radiance = 0;
#ifdef GI
	radiance = Color::GammaToLinear(FULLRES_LOAD(srcDiffuse, pixCoord, uv * frameScale, samplerLinearClamp).rgb * GIStrength);
#	ifdef GI_BOUNCE
	radiance += prev_ambient.rgb * GIBounceFade;
#	endif
	outRadianceDisocc[pixCoord] = radiance;
#endif

#ifdef TEMPORAL_DENOISER
	accum_frames = max(1, min(accum_frames * 255 + 1, MaxAccumFrames));
	outAccumFrames[pixCoord] = accum_frames / 255.0;
	outRemappedAo[pixCoord] = prev_ao;
	outRemappedIlY[pixCoord] = prev_y;
	outRemappedIlCoCg[pixCoord] = prev_co_cg;
	outRemappedPrevGISpecular[pixCoord] = prev_gi_specular;
#endif
}