#ifndef __SNOW_LIGHTING_HLSLI__
#define __SNOW_LIGHTING_HLSLI__

#if (defined(LODLANDSCAPE) || defined(LODLANDNOISE) || defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(PROJECTED_UV) || defined(LANDSCAPE)) && !defined(WORLD_MAP) && !defined(TRUE_PBR)
#	include "Common/PBR.hlsli"
#endif
#if defined(EMAT) && !defined(LANDSCAPE) && !defined(SKINNED) && !defined(FACEGEN) && !defined(EYE)
#	include "SnowDeformation/SnowParallaxFollow.hlsli"
#endif
#include "SnowDeformation/SnowColor.hlsli"
#include "SnowDeformation/SnowCommon.hlsli"

namespace SnowLighting
{
	struct State
	{
		bool projMatch;
		float3 projAlbedo;
		float4 projRMAOS;
		float3 parallaxShift;
		bool underWater;
		float lodReplaceW;
		float3 lodAlbedo;
		bool texMatch;
		float texWeight;
		float3 projLightTotal;
		bool landRecolor;
		uint landSnowBits;
		float3 landSnowRaw;
		float2 landSnowNormal;
		float4 landSnowRMAOS;
		float landSnowFrac;
	};

	// Triplanar reads with gradients taken outside the caller's branch.
	float4 TriplanarGrad(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float3 dPdx, float3 dPdy)
	{
		return tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz) * weights.x +
		       tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz) * weights.y +
		       tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy) * weights.z;
	}

	float4 TriplanarStochasticGrad(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float noise, float3 dPdx, float3 dPdy)
	{
		float4 result = 0;
		if (noise < weights.x)
			result = tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		else if (noise < weights.x + weights.y)
			result = tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		else
			result = tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
		return result;
	}

	float LandSnowTexel(float3 g)
	{
		SNOW_TEXEL_SCORE(score, g, SharedData::snowDeformationSettings.LandRecolorLumLo, 0.18);
		return score;
	}

	float3 SampleEncoded(float3 sampled)
	{
#if defined(TRUE_PBR)
		return TransferFunctions::LinearToGamma22(max(sampled, 0.0));
#else
		return sampled;
#endif
	}

#if defined(LANDSCAPE)
	float3 LayerEncoded(float3 layerColor)
	{
#	if defined(TRUE_PBR)
		return TransferFunctions::LinearToGamma22(max(layerColor, 0.0));
#	elif defined(ENABLE_LL)
#		if defined(ENABLE_ACESCG)
		layerColor = AP1TosRGB(layerColor);
#		endif
		const float white = SharedData::linearLightingSettings.diffuseWhiteReflectance;
		const float k = SharedData::linearLightingSettings.diffuseCurve;
		const float3 y = clamp(layerColor, 0.0, white * 0.999);
		return pow(saturate(y / max(white * k - y * (k - 1.0), 1e-6)), 1.0 / SharedData::linearLightingSettings.diffuseGamma);
#	else
		return layerColor;
#	endif
	}
#endif

	void SnowGrain(inout float3 worldNormal, float3 texN, float3 triWeights, float screenNoise, float weight)
	{
		const bool planeX = screenNoise < triWeights.x;
		const bool planeZ = !planeX && screenNoise >= triWeights.x + triWeights.y;
		float3 bumpT = planeX ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
		float3 bumpB = planeZ ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
		bumpT -= worldNormal * dot(worldNormal, bumpT);
		bumpB -= worldNormal * dot(worldNormal, bumpB);
		bumpT *= rsqrt(max(dot(bumpT, bumpT), 1e-8));
		bumpB *= rsqrt(max(dot(bumpB, bumpB), 1e-8));
		worldNormal = normalize(worldNormal + (bumpT * texN.x + bumpB * texN.y) * weight);
	}

	float4 ShellRMAOS(float4 r)
	{
		return float4(clamp(r.x * SharedData::snowDeformationSettings.SnowRoughnessScale, 0.05, 1.0), 0.0, r.z, r.w * SharedData::snowDeformationSettings.SnowSpecularLevel);
	}

	bool ShellHasRMAOS()
	{
		return (uint(SharedData::snowDeformationSettings.DebugTerrainOverlay) & 512) != 0;
	}
	static const float4 kShellRMAOSDefault = float4(0.6, 0.0, 1.0, 0.028);

#if defined(LANDSCAPE) || ((defined(LODLANDSCAPE) || defined(LODLANDNOISE) || defined(LODOBJECTS) || defined(LODOBJECTSHD)) && !defined(WORLD_MAP) && !defined(TRUE_PBR))
	struct SnowSetTaps
	{
		float2 uv0, uv1, uv2;
		float3 weights;
		float2 dx, dy;
	};

	float2 SnowSetHash(float2 cell)
	{
		float3 p3 = frac(float3(cell.x, cell.y, cell.x) * float3(0.1031, 0.1030, 0.0973));
		p3 += dot(p3, p3.yzx + 33.33);
		return frac(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
	}

	SnowSetTaps SnowSetTapsAt(float2 worldRelXY, float2 worldXY)
	{
		float2 uv = frac(FrameBuffer::CameraPosAdjust.xy / SnowDeformation::SnowUVTile) + worldRelXY / SnowDeformation::SnowUVTile;
		SnowSetTaps t;
		t.dx = ddx(uv);
		t.dy = ddy(uv);
		float2 lattice = mul(float2x2(1.0, -0.57735027, 0.0, 1.15470054), worldXY * (0.6 / 256.0));
		float2 cellBase = floor(lattice);
		float2 f = frac(lattice);
		bool upper = f.x + f.y >= 1.0;
		float2 v0 = cellBase + (upper ? float2(1.0, 1.0) : float2(0.0, 0.0));
		float2 v1 = cellBase + (upper ? float2(0.0, 1.0) : float2(1.0, 0.0));
		float2 v2 = cellBase + (upper ? float2(1.0, 0.0) : float2(0.0, 1.0));
		float3 bary = upper ? float3(f.x + f.y - 1.0, 1.0 - f.x, 1.0 - f.y) : float3(1.0 - f.x - f.y, f.x, f.y);
		bary = pow(bary, 4.0);
		bary /= dot(bary, 1.0);
		t.weights = bary;
		t.uv0 = uv + SnowSetHash(v0);
		t.uv1 = uv + SnowSetHash(v1);
		t.uv2 = uv + SnowSetHash(v2);
		return t;
	}

	float4 SnowSetSample(Texture2D<float4> tex, SnowSetTaps t)
	{
		return t.weights.x * tex.SampleGrad(SampColorSampler, t.uv0, t.dx, t.dy) + t.weights.y * tex.SampleGrad(SampColorSampler, t.uv1, t.dx, t.dy) +
		       t.weights.z * tex.SampleGrad(SampColorSampler, t.uv2, t.dx, t.dy);
	}
#endif

#if defined(LANDSCAPE)
	void WriteLandSnowOdds(float4 blendWeights1, float2 blendWeights2, inout float4 masks)
	{
		[branch] if ((SharedData::snowDeformationSettings.DebugTerrainOverlay & 128) != 0)
		{
			uint bits = (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowLandIsSnowMask) >> Permutation::ExtraFeatureFlags::SnowLandIsSnowShift;
			float snow = dot(blendWeights1, float4(bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1)) + dot(blendWeights2, float2((bits >> 4) & 1, (bits >> 5) & 1));
			float rest = max(dot(blendWeights1, 1.0) + blendWeights2.x + blendWeights2.y - snow, 0.0);
			float odds = clamp(log2(max(snow, 1e-12)) - log2(max(rest, 1e-12)), -16.0, 16.0);
			masks.y = 0.004 + 0.996 * (odds + 16.0) * (1.0 / 32.0);
			masks.w = 1.0;
		}
	}

#	if defined(EMAT) && defined(DEFERRED)
	void LandSnowEdgeBlend(PS_INPUT input, float viewDepth, float screenNoise, float2 uv, float mipLevels[6], DisplacementParams params[6], StochasticOffsets sharedOffset, inout float weights[6])
	{
		[branch] if ((SharedData::snowDeformationSettings.DebugTerrainOverlay & 128) != 0 && SharedData::extendedMaterialSettings.EnableHeightBlending && viewDepth > 1024.0)
		{
			uint bits = (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowLandIsSnowMask) >> Permutation::ExtraFeatureFlags::SnowLandIsSnowShift;
			float snow = dot(input.LandBlendWeights1, float4(bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1)) + dot(input.LandBlendWeights2.xy, float2((bits >> 4) & 1, (bits >> 5) & 1));
			float rest = dot(input.LandBlendWeights1, 1.0) + input.LandBlendWeights2.x + input.LandBlendWeights2.y - snow;
			[branch] if (snow > 0.01 && rest > 0.01)
			{
				float full[6];
				ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, params, 1.0,
					smoothstep(0.0, 1.0, input.LandBlendWeights1), smoothstep(0.0, 1.0, input.LandBlendWeights2.xy), sharedOffset, full);
				float far = smoothstep(1024.0 * 1024.0, 2048.0 * 2048.0, viewDepth * viewDepth);
				[unroll] for (uint i = 0; i < 6; i++)
					weights[i] = lerp(weights[i], full[i], far);
			}
		}
	}
#	endif

	void LandRecolorBegin(inout State s, float3 worldRel)
	{
		uint bits = (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowLandIsSnowMask) >> Permutation::ExtraFeatureFlags::SnowLandIsSnowShift;
		[branch] if ((SharedData::snowDeformationSettings.DebugTerrainOverlay & 256) != 0 && bits != 0)
		{
			float2 worldXY = worldRel.xy + FrameBuffer::CameraPosAdjust.xy;
			const SnowSetTaps taps = SnowSetTapsAt(worldRel.xy, worldXY);

			float3 a = SnowSetSample(SnowDeformation::HorizonSnowAlbedo, taps).rgb;
			float3 shellAlbedo = SnowColor::SetAlbedo(a, SharedData::snowDeformationSettings.SnowIsLinear > 0.5);
			const float wet = SharedData::snowDeformationSettings.LandWetPrint;
			const float ice = SharedData::snowDeformationSettings.LandIce;
			float press = 0.0;
			float glaze = 0.0;
			[branch] if (max(wet, ice) > 0.001)
			{
				const float pressed = smoothstep(0.1, 0.5, SnowDeformation::GetDeformation(worldXY));
				press = pressed * saturate(wet * 4.0);
				float since = 0.0;
				[branch] if (pressed > 0.001 && ice > 0.001)
					since = max(ice - SnowIce::AtCut(SnowDeformation::GetCutDate(worldXY)).y, 0.0);
				glaze = max(pressed * since, 0.1 * ice);
			}
			const float3 printTint = lerp(1.0, float3(0.76, 0.77, 0.80) * (1.0 - 0.25 * wet), press) * lerp(1.0, float3(0.93, 0.955, 0.985), glaze);
			shellAlbedo *= printTint;
#	if defined(TRUE_PBR)
			s.landSnowRaw = Color::PBRMaterialToLinear(shellAlbedo);
#	else
			s.landSnowRaw = shellAlbedo;
			s.lodAlbedo = shellAlbedo;
#	endif
			s.landSnowNormal = 0.0;
			[branch] if (SharedData::snowDeformationSettings.SnowHasNormal > 0.5)
			{
				float2 n = SnowSetSample(SnowDeformation::HorizonSnowNormal, taps).xy * 2.0 - 1.0;
				s.landSnowNormal = float2(n.x, -n.y);
			}
			s.landSnowRMAOS = kShellRMAOSDefault;
			[branch] if (ShellHasRMAOS())
				s.landSnowRMAOS = ShellRMAOS(SnowSetSample(SnowDeformation::HorizonSnowRmaos, taps));
			s.landSnowRMAOS.x *= 1.0 - max(press * (0.24 + 0.26 * wet), glaze * 0.8);
			s.landSnowBits = bits;
			s.landRecolor = true;
		}
	}

	void LandRecolorLayer(inout State s, uint tile, float weight, inout float3 layerColor, inout float3 layerNormal
#	if defined(TRUE_PBR)
		,
		inout float4 layerRMAOS
#	endif
	)
	{
		[branch] if (s.landRecolor && ((s.landSnowBits >> tile) & 1) != 0)
		{
			float w = LandSnowTexel(LayerEncoded(layerColor));
			layerColor = lerp(layerColor, s.landSnowRaw, w);
			layerNormal = lerp(layerNormal, float3(0.5, 0.5, 1.0), w);
#	if defined(TRUE_PBR)
			layerRMAOS = lerp(layerRMAOS, s.landSnowRMAOS, w);
#	endif
			s.landSnowFrac += weight * w;
		}
	}

	void LandRecolorNormal(inout State s, inout float3 worldNormal)
	{
		[branch] if (s.landSnowFrac > 0.003 && SharedData::snowDeformationSettings.SnowHasNormal > 0.5)
		{
			SNOW_TANGENT_FRAME(bumpT, bumpB, worldNormal);
			worldNormal = normalize(worldNormal + (bumpT * s.landSnowNormal.x + bumpB * s.landSnowNormal.y) * saturate(s.landSnowFrac));
		}
#	if !defined(TRUE_PBR) && !defined(WORLD_MAP)
		s.lodReplaceW = s.landRecolor ? saturate(s.landSnowFrac) : 0.0;
#	endif
	}
#endif

#if (defined(LODLANDSCAPE) || defined(LODLANDNOISE) || defined(LODOBJECTS) || defined(LODOBJECTSHD)) && !defined(WORLD_MAP) && !defined(TRUE_PBR)
	void LODRecolor(inout State s, float3 rawBaseColor, float2 uv, float2 worldXYRel, float3 normal)
	{
#	if defined(LODOBJECTS) || defined(LODOBJECTSHD)
		bool snowLodOn = SharedData::snowDeformationSettings.LODObjectEnable > 0.5 &&
		                 (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowLODBakedIsSnow) != 0;
		const float coverScale = exp2(SharedData::MipBias + 2.0);
		const float2 coverDx = ddx(uv) * coverScale;
		const float2 coverDy = ddy(uv) * coverScale;
#	else
		bool snowLodOn = SharedData::snowDeformationSettings.LODReplaceEnable > 0.5;
#	endif
		[branch] if (snowLodOn)
		{
			float lodSnowScore = SnowDeformation::ClassifyLODSnow(rawBaseColor);
			float lodReplaceT = saturate((length(worldXYRel) - SharedData::snowDeformationSettings.LODReplaceStart) * SharedData::snowDeformationSettings.LODReplaceFadeInv);
#	if defined(LODOBJECTS) || defined(LODOBJECTSHD)
			float lodReplaceW = lodSnowScore * lodReplaceT * smoothstep(0.0, 0.3, normal.z);
			[branch] if (lodReplaceW > 0.003)
				lodReplaceW *= TexColorSampler.SampleGrad(SampColorSampler, uv, coverDx, coverDy).a >= 254.5 / 255.0 ? 1.0 : 0.0;
#	else
			float lodReplaceW = lodSnowScore * lodReplaceT * smoothstep(0.35, 0.65, normal.z);
#	endif
			const SnowSetTaps taps = SnowSetTapsAt(worldXYRel, worldXYRel + FrameBuffer::CameraPosAdjust.xy);
			[branch] if (lodReplaceW > 0.003)
			{
				float3 snowSample = SnowSetSample(SnowDeformation::HorizonSnowAlbedo, taps).rgb;
				s.lodReplaceW = lodReplaceW;
				s.lodAlbedo = SnowColor::SetAlbedo(snowSample, SharedData::snowDeformationSettings.SnowIsLinear > 0.5);
			}
		}
	}
#endif

#if (defined(LODLANDSCAPE) || defined(LODLANDNOISE) || defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(LANDSCAPE)) && !defined(WORLD_MAP) && !defined(TRUE_PBR)
	void RelightLOD(State s, DirectContext dirLightContext, IndirectContext indirectContext, float3 directionalAmbientColor,
		inout float3 color, inout float3 outputAlbedo, inout float3 specularColor, inout float3 lobeSpecular, inout float roughness)
	{
		[branch] if (s.lodReplaceW > 0.003)
		{
			MaterialProperties snowMaterial = (MaterialProperties)0;
			snowMaterial.BaseColor = s.lodAlbedo;
			snowMaterial.Roughness = clamp(0.6 * SharedData::snowDeformationSettings.SnowRoughnessScale, 0.05, 1.0);
			snowMaterial.F0 = 0.028;
			snowMaterial.AO = 1.0;
			DirectContext snowContext = dirLightContext;
			snowContext.lightColor *= Color::PBRLightingCompensation;
			DirectLightingOutput snowLit;
			PBR::GetDirectLightInput(snowLit, snowContext, snowMaterial, float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), 0.0.xx);
			IndirectLobeWeights snowLobes;
			PBR::GetIndirectLobeWeights(snowLobes, indirectContext, snowMaterial);
			float3 snowColor = (snowLit.diffuse * snowMaterial.BaseColor + snowLobes.diffuse * directionalAmbientColor) * Color::PBRLightingScale;
			color = lerp(color, snowColor, s.lodReplaceW);
			outputAlbedo = lerp(outputAlbedo, snowLobes.diffuse * Color::PBRLightingScale, s.lodReplaceW);
			specularColor = lerp(specularColor, snowLit.specular * Color::PBRLightingScale, s.lodReplaceW);
			lobeSpecular = lerp(lobeSpecular, snowLobes.specular, s.lodReplaceW);
			roughness = lerp(roughness, snowMaterial.Roughness, s.lodReplaceW);
		}
	}
#endif

#if !defined(LANDSCAPE)
	void BeginObject(inout State s, float3 worldRel)
	{
		[branch] if (SharedData::snowDeformationSettings.ProjSnowEnable > 0.5 || SharedData::snowDeformationSettings.SnowTexturedEnable > 0.5)
			s.underWater = SnowDeformation::UnderWater(worldRel + FrameBuffer::CameraPosAdjust.xyz);
	}
#endif

#if defined(PROJECTED_UV)
	float ProjectionAlpha(State s, float vertexAlpha, float projDot)
	{
		[flatten] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowProjectedNoAlpha) != 0)
			vertexAlpha = 1.0;
		[flatten] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowProjectedUnauthored) != 0)
			vertexAlpha *= smoothstep(SharedData::snowDeformationSettings.ProjUnauthoredThreshold - 0.1, SharedData::snowDeformationSettings.ProjUnauthoredThreshold + 0.1, projDot);
		[flatten] if (s.underWater && (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowProjectedIsSnow) != 0)
			vertexAlpha = 0.0;
		return vertexAlpha;
	}

#	if defined(EMAT) && !defined(LANDSCAPE) && !defined(SKINNED) && !defined(FACEGEN) && !defined(EYE)
	float ParallaxProjNoise(inout State s, float projNoise, float3 worldRel, float3 triWeights, float2 uv, float2 uvOriginal)
	{
		[branch] if (SharedData::snowDeformationSettings.ParallaxFollow > 0.5 && SharedData::snowDeformationSettings.ProjSnowEnable > 0.5 &&
					 (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowProjectedIsSnow) != 0)
		{
			const float3 pDx = ddx_fine(worldRel);
			const float3 pDy = ddy_fine(worldRel);
			s.parallaxShift = SnowParallaxFollow::UVShiftToWorld(uv - uvOriginal, ddx_fine(uvOriginal), ddy_fine(uvOriginal), pDx, pDy);
#		if defined(TRUE_PBR)
			[flatten] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
				s.parallaxShift = 0.0;
#		endif
			[branch] if (any(s.parallaxShift != 0.0))
			{
				const float tiling = ProjectedUVParams.z;
				projNoise = TriplanarGrad(TexCharacterLightProjNoiseSampler, SampCharacterLightProjNoiseSampler, worldRel + FrameBuffer::CameraPosAdjust.xyz + s.parallaxShift,
					triWeights, tiling, pDx * tiling, pDy * tiling).x;
			}
		}
		return projNoise;
	}

#		if !defined(MODELSPACENORMALS) && !defined(MULTI_LAYER_PARALLAX) && !defined(PARALLAX)
#			define SNOW_FLATTEN_PARALLAX
	float ParallaxUnderSnow(float3 worldRel, float3 vertexNormal, float3 texProj, float vertexAlpha)
	{
		float keep = 1.0;
		[branch] if (SharedData::snowDeformationSettings.ParallaxFlatten > 0.5 && SharedData::snowDeformationSettings.ProjSnowEnable > 0.5 &&
					 (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowProjectedIsSnow) != 0)
		{
#			if defined(TREE_ANIM)
			vertexAlpha = 1.0;
#			endif
			State s = (State)0;
			s.underWater = SnowDeformation::UnderWater(worldRel + FrameBuffer::CameraPosAdjust.xyz);
			const float projDot = dot(vertexNormal, normalize(texProj));
			const float coverWeight = projDot * ProjectionAlpha(s, vertexAlpha, projDot) - ProjectedUVParams.w;
			keep = 1.0 - smoothstep(-0.1, ProjectedUVParams.x + 0.1, coverWeight);
		}
		return keep;
	}
#		endif
#	endif

#	if !defined(FACEGEN) && !defined(MULTI_LAYER_PARALLAX) && !defined(PARALLAX)
#		if defined(SPARKLE)
#			define SNOW_PROJ_SAMPLER SampColorSampler
#		else
#			define SNOW_PROJ_SAMPLER SampProjDiffuseSampler
#		endif
	void ProjectedRecolor(inout State s, float projWeight, float3 vertexNormal, float3 projWorldPos, float screenNoise,
		inout float projectedMaterialWeight, inout float3 baseColor, inout float4 rawRMAOS, inout float3 worldNormal)
	{
		s.projMatch = SharedData::snowDeformationSettings.ProjSnowEnable > 0.5 && !s.underWater &&
		              (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowProjectedIsSnow) != 0;
		[branch] if (s.projMatch)
		{
			float snowVerdict = smoothstep(-0.01, 0.01, projWeight);
			projectedMaterialWeight = snowVerdict;
			[branch] if (snowVerdict > 0.003)
			{
				float3 snowTriWeights = Triplanar::GetWeights(vertexNormal, vertexNormal);
				const float3 snowPos = projWorldPos + s.parallaxShift;
				float3 snowGradX, snowGradY;
				Triplanar::ComputeGradients(projWorldPos, 1.0 / SnowDeformation::SnowUVTile, snowGradX, snowGradY);
				[branch] if (SharedData::snowDeformationSettings.ProjSnowNormalEnable > 0.5 && SharedData::snowDeformationSettings.SnowHasNormal > 0.5)
				{
					worldNormal = normalize(lerp(worldNormal, vertexNormal, snowVerdict));
					float3 texN = TriplanarStochasticGrad(SnowDeformation::HorizonSnowNormal, SNOW_PROJ_SAMPLER, snowPos, snowTriWeights, 1.0 / SnowDeformation::SnowUVTile, screenNoise, snowGradX, snowGradY).xyz * 2.0 - 1.0;
					SnowGrain(worldNormal, texN, snowTriWeights, screenNoise, snowVerdict);
				}
				[branch] if (projectedMaterialWeight > 0.003)
				{
				float3 snowProjSample = TriplanarStochasticGrad(SnowDeformation::HorizonSnowAlbedo, SNOW_PROJ_SAMPLER, snowPos, snowTriWeights, 1.0 / SnowDeformation::SnowUVTile, screenNoise, snowGradX, snowGradY).xyz;
				s.projAlbedo = SnowColor::SetAlbedo(snowProjSample, SharedData::snowDeformationSettings.SnowIsLinear > 0.5);
				s.projRMAOS = kShellRMAOSDefault;
				[branch] if (ShellHasRMAOS())
				{
					s.projRMAOS = ShellRMAOS(TriplanarStochasticGrad(SnowDeformation::HorizonSnowRmaos, SNOW_PROJ_SAMPLER, snowPos, snowTriWeights, 1.0 / SnowDeformation::SnowUVTile, screenNoise, snowGradX, snowGradY));
				}
#		if defined(TRUE_PBR)
				baseColor = lerp(baseColor, s.projAlbedo, projectedMaterialWeight);
				rawRMAOS = lerp(rawRMAOS, s.projRMAOS, projectedMaterialWeight);
#		else
				baseColor = lerp(baseColor, s.projAlbedo, projectedMaterialWeight);
#		endif
				}
			}
		}
	}
#		undef SNOW_PROJ_SAMPLER
#	endif
#endif

#if defined(PROJECTED_UV) && !defined(TRUE_PBR) && !defined(WORLD_MAP)
	void RelightProjected(State s, float projectedMaterialWeight, DirectContext dirLightContext, IndirectContext indirectContext, float3 directionalAmbientColor,
		float3 dirLightColor, float3 worldNormal, float dirDetailedShadow,
		inout float3 color, inout float3 outputAlbedo, inout float3 specularColor, inout float3 lobeSpecular, inout float roughness)
	{
		[branch] if (s.projMatch && projectedMaterialWeight > 0.003)
		{
			MaterialProperties snowMaterial = (MaterialProperties)0;
			snowMaterial.BaseColor = s.projAlbedo;
			snowMaterial.Roughness = s.projRMAOS.x;
			snowMaterial.F0 = s.projRMAOS.w;
			snowMaterial.AO = s.projRMAOS.z;
			DirectContext snowContext = dirLightContext;
			snowContext.lightColor *= Color::PBRLightingCompensation;
			DirectLightingOutput snowLit;
			PBR::GetDirectLightInput(snowLit, snowContext, snowMaterial, float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), 0.0.xx);
			IndirectLobeWeights snowLobes;
			PBR::GetIndirectLobeWeights(snowLobes, indirectContext, snowMaterial);
			float3 vanillaSunTerm = dirLightColor * saturate(dot(worldNormal, DirLightDirection.xyz)) * dirDetailedShadow;
			float3 pointLightShare = max(0.0, s.projLightTotal - vanillaSunTerm);
			float3 snowColor = (snowLit.diffuse * snowMaterial.BaseColor + pointLightShare * snowMaterial.BaseColor + snowLobes.diffuse * directionalAmbientColor) * Color::PBRLightingScale;
			color = lerp(color, snowColor, projectedMaterialWeight);
			outputAlbedo = lerp(outputAlbedo, snowLobes.diffuse * Color::PBRLightingScale, projectedMaterialWeight);
			specularColor = lerp(specularColor, snowLit.specular * Color::PBRLightingScale, projectedMaterialWeight);
			lobeSpecular = lerp(lobeSpecular, snowLobes.specular, projectedMaterialWeight);
			roughness = lerp(roughness, snowMaterial.Roughness, projectedMaterialWeight);
		}
	}
#endif

#if !defined(WORLD_MAP) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE) && !defined(LODLANDNOISE) && !defined(LODOBJECTS) && !defined(LODOBJECTSHD)
	void SnowTexturedRecolor(inout State s, float3 rawBaseColor, float3 vertexNormal, float3 worldRel, float screenNoise,
		inout float3 baseColor, inout float4 rawRMAOS, inout float3 worldNormal)
	{
		[branch] if (SharedData::snowDeformationSettings.SnowTexturedEnable > 0.5 && !s.underWater &&
					 (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowLODBakedIsSnow) != 0)
		{
			s.texMatch = true;
			float texel = (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowTexturedGround) != 0 ? 1.0 : LandSnowTexel(SampleEncoded(rawBaseColor));
			s.texWeight = texel * smoothstep(0.35, 0.65, vertexNormal.z);
			[branch] if (s.texWeight > 0.003)
			{
				float3 snowTexWorld = worldRel + FrameBuffer::CameraPosAdjust.xyz;
				float3 snowTexWeights = Triplanar::GetWeights(vertexNormal, vertexNormal);
				[branch] if (SharedData::snowDeformationSettings.ProjSnowNormalEnable > 0.5 && SharedData::snowDeformationSettings.SnowHasNormal > 0.5)
				{
					worldNormal = normalize(lerp(worldNormal, vertexNormal, s.texWeight));
					float3 texN = Triplanar::SampleStochastic(SnowDeformation::HorizonSnowNormal, SampColorSampler, snowTexWorld, snowTexWeights, 1.0 / SnowDeformation::SnowUVTile, screenNoise).xyz * 2.0 - 1.0;
					SnowGrain(worldNormal, texN, snowTexWeights, screenNoise, s.texWeight);
				}
				float3 snowTexSample = Triplanar::SampleStochastic(SnowDeformation::HorizonSnowAlbedo, SampColorSampler, snowTexWorld, snowTexWeights, 1.0 / SnowDeformation::SnowUVTile, screenNoise).xyz;
				float3 snowTexAlbedo = SnowColor::SetAlbedo(snowTexSample, SharedData::snowDeformationSettings.SnowIsLinear > 0.5);
#	if defined(TRUE_PBR)
				baseColor = lerp(baseColor, snowTexAlbedo, s.texWeight);
				float4 snowTexRMAOS = kShellRMAOSDefault;
				[branch] if (ShellHasRMAOS())
					snowTexRMAOS = ShellRMAOS(Triplanar::SampleStochastic(SnowDeformation::HorizonSnowRmaos, SampColorSampler, snowTexWorld, snowTexWeights, 1.0 / SnowDeformation::SnowUVTile, screenNoise));
				rawRMAOS = lerp(rawRMAOS, snowTexRMAOS, s.texWeight);
#	else
				baseColor = lerp(baseColor, snowTexAlbedo, s.texWeight);
#	endif
			}
		}
	}
#endif

#if !defined(LANDSCAPE)
	void WriteMasksY(State s, float projWeight, inout float masksY)
	{
#	if defined(PROJECTED_UV)
		[flatten] if (s.projMatch)
		{
			float snowBareT = projWeight >= -0.3 ? 1.0 + projWeight * ((5.0 / 6.0) / 0.3) : (projWeight + 0.8) * ((1.0 / 6.0) / 0.5);
			masksY = projWeight >= 0.0 ? 48.0 : (snowBareT > 0.0 ? 4.0 * exp2(3.0 * snowBareT) : 2.0);
		}
#	else
		[flatten] if ((Permutation::ExtraFeatureDescriptor & (Permutation::ExtraFeatureFlags::SnowProjectedUnauthored | Permutation::ExtraFeatureFlags::SnowMultipassBase | Permutation::ExtraFeatureFlags::SnowObjectBarePass)) != 0)
			masksY = 2.0;
#	endif
#	if !defined(WORLD_MAP) && !defined(LODLANDSCAPE) && !defined(LODLANDNOISE) && !defined(LODOBJECTS) && !defined(LODOBJECTSHD)
		[flatten] if (s.texMatch)
			masksY = (s.texWeight >= 0.5 && (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::SnowTexturedOverlay) == 0) ? 48.0 : 2.0;
#	endif
#	if (defined(LODOBJECTS) || defined(LODOBJECTSHD)) && !defined(WORLD_MAP) && !defined(TRUE_PBR)
		[flatten] if (s.lodReplaceW > 0.003)
			masksY = s.lodReplaceW >= 0.5 ? 48.0 : 2.0;
#	endif
		[flatten] if (s.underWater)
			masksY = 2.0;
#	if defined(LODOBJECTS) || defined(LODOBJECTSHD)
		[flatten] if (masksY >= 1.5)
			masksY *= 256.0;
#	endif
	}
#endif

}

#if defined(LANDSCAPE)
#	undef LIGHTING_LANDSCAPE_LAYER_HOOK
#	if defined(TRUE_PBR)
#		define LIGHTING_LANDSCAPE_LAYER_HOOK(TILE) SnowLighting::LandRecolorLayer(snowState, TILE, weight, landColorRGB, landNormalRGB, landRMAOS);
#	else
#		define LIGHTING_LANDSCAPE_LAYER_HOOK(TILE) SnowLighting::LandRecolorLayer(snowState, TILE, weight, landColorRGB, landNormalRGB);
#	endif
#endif

#endif
