#include "Features/ProceduralGrass.h"

#include "Features/LinearLighting.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace PGrassCommon;

namespace
{
	/** @brief Cubic smoothstep; equal edges produce a step at edge0. */
	float Smoothstep(float edge0, float edge1, float value)
	{
		if (edge0 == edge1)
			return value < edge0 ? 0.0f : 1.0f;
		const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	/** @brief Roughness-dependent coefficients of PBR::FuzzDirectionalAlbedo's MaterialX fit. */
	float4 GetFuzzDirectionalAlbedoParameters(float roughness)
	{
		const float r = std::clamp(roughness, 0.01f, 1.0f);
		const float s = r * (0.0206607f + 1.58491f * r) / (0.0379424f + r * (1.32227f + r));
		const float m = r * (-0.193854f + r * (-1.14885f + r * (1.7932f - 0.95943f * r * r))) / (0.046391f + r);
		const float o = r * (0.000654023f + (-0.0207818f + 0.119681f * r) * r) / (1.26264f + r * (-1.92021f + r));
		return float4(1.0f / s, m, 1.0f / (s * std::sqrt(2.0f * std::numbers::pi_v<float>)), o);
	}
}

std::string ProceduralGrass::LandTextureKey(const RE::TESLandTexture* tex)
{
	if (!tex)
		return {};
	const RE::TESFile* file = tex->GetFile(0);
	if (!file)
		return {};
	return std::format("{}|0x{:06X}", file->GetFilename(), tex->GetLocalFormID());
}

void ProceduralGrass::RebuildTypeAllocation()
{
	typeAllocation.clear();
	textureSelection.clear();
	textureSelectionByTexture.clear();
	grassCellCachePolicyDirty = true;
	grassTypesDirty = true;
	typeAllocation.reserve(PGrassCommon::MaxGrassTypes - 2);
	textureSelection.reserve(settings.textureTypes.size());
	textureSelectionByTexture.reserve(settings.textureTypes.size());

	// Sort keys so cached type ids remain stable across frames.
	std::vector<std::string> keys;
	keys.reserve(settings.textureTypes.size());
	for (const auto& [key, defs] : settings.textureTypes)
		if (!defs.empty())
			keys.push_back(key);

	std::sort(keys.begin(), keys.end());

	// Slots 0 and 1 are reserved for bare and base grass.
	for (const auto& key : keys) {
		const auto& defs = settings.textureTypes[key];
		TextureSelection sel;
		float acc = 0.0f;

		for (uint32_t i = 0; i < defs.size(); i++) {
			if (defs[i].noGrass) {
				sel.ids.push_back(0u);
			} else {
				if (typeAllocation.size() + 2 >= PGrassCommon::MaxGrassTypes)
					break;  // The type pool is full; this texture keeps only the variants allocated so far.
				sel.ids.push_back(static_cast<uint8_t>(typeAllocation.size() + 2));
				typeAllocation.emplace_back(key, i);
			}
			acc += std::max(0.0f, defs[i].weight);
			sel.cumulative.push_back(acc);
		}

		sel.total = acc;
		if (!sel.ids.empty())
			textureSelection[key] = std::move(sel);
	}
}

PGrassCommon::GrassType ProceduralGrass::BuildGrassType(const GrassTypeFields& s)
{
	const auto packColor = [](const float3& c) { return float4(c.x, c.y, c.z, 0.0f); };
	constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;

	PGrassCommon::GrassType t{};
	t.height = s.grassHeight;
	t.width = s.grassWidth;
	t.minSlope = std::cos(s.grassMinSlope * degreesToRadians);
	t.maxSlope = std::cos(s.grassMaxSlope * degreesToRadians);
	t.stiffness = s.stiffness;
	t.rotationalStiffness = s.rotationalStiffness;
	t.tipWeight = s.tipWeight;
	t.mid = s.mid;

	// Keep the generator's reciprocal grid size finite and interpolated clump heights within the type bounds.
	t.clumpGridSize = std::clamp(s.clumpGridSize, MinClumpGridSize, MaxClumpGridSize);
	t.clumpDistanceFactor = s.clumpDistanceFactor;
	t.clumpFacingFactor = s.clumpFacingFactor;
	t.clumpLeanFactor = s.clumpLeanFactor;
	t.clumpHeightFactor = std::clamp(s.clumpHeightFactor, 0.0f, 1.0f);
	t.clumpAOStrength = s.clumpAOStrength;
	t.clumpColorStrength = s.grassClumpColorStrength;

	t.minAO = s.ao;
	t.specular = s.specular;
	t.specularAnisotropy = std::clamp(s.specularAnisotropy, 0.0f, 1.0f);
	t.minMaxSubsurfaceOpacity = s.subsurfaceOpacity;
	t.grassSubsurfaceColor = packColor(s.grassSubsurfaceTint);
	t.grassSubsurfaceColor.w = std::clamp(s.sheenTint, 0.0f, 1.0f);
	t.grassSurfParams = float4(s.sheenStrength, std::clamp(s.grassTransmissionStrength, 0.0f, 2.0f), std::clamp(s.grassSlopeFacing, 0.0f, 1.0f), s.sheenRoughness);

	const float3 rough = s.baseMinTipRoughness;
	const float roughnessStart = s.tipRoughnessStart;
	t.baseMinTipRoughnessStart = float4(rough.x, rough.y, rough.z, roughnessStart);

	// Fit Mid roughness at its three vertex positions to avoid evaluating both smoothstep curves in the vertex shader.
	const float roughnessAtMidFirst = std::lerp(rough.x, rough.y, Smoothstep(0.0f, roughnessStart, 0.5f));
	const float roughnessAtMid = std::lerp(roughnessAtMidFirst, rough.z, Smoothstep(rough.x, 1.0f, 0.5f));
	const float baseToMid = roughnessAtMid - rough.x;
	const float baseToTip = rough.z - rough.x;
	t.midRoughnessPolynomial = float4(2.0f * baseToTip - 8.0f * baseToMid, 8.0f * baseToMid - baseToTip, rough.x, 0.0f);

	t.grassTypeLightParams = float4(s.grassBounceStrength, s.curvedNormalStrength, 0.0f, s.grassAmbientDesat);

	t.baseColor = packColor(s.baseColor);
	t.tipColor = packColor(s.tipColor);
	t.grassColorTipDry = packColor(s.grassColorTipDry);
	t.grassColorVar = float4(s.grassColorHueVariation, s.grassColorValueVariation, s.grassColorTipDryStrength, s.grassColorMottleStrength);
	t.grassColorCool = packColor(s.grassColorCool);
	t.grassColorWarm = packColor(s.grassColorWarm);
	t.grassBounceColor = packColor(s.grassBounceColor);

	return t;
}

void ProceduralGrass::ResolveGrassTypes(const bool prelinearizeTypeColors)
{
	// Slot 0 is bare, slot 1 is base grass, and later slots are texture variants.
	resolvedGrassTypes = {};
	resolvedGeneratorTypes = {};
	resolvedGrassTypes.grassType[1] = ResolveGrassType(nlohmann::json::object());

	for (size_t i = 0; i < typeAllocation.size(); i++) {
		const auto& [key, defIndex] = typeAllocation[i];
		resolvedGrassTypes.grassType[i + 2] = ResolveGrassType(settings.textureTypes[key][defIndex].overrides);
	}

	const auto convertTint = [](float4& tint) {
		globals::features::linearLighting.SRGBToWorking(&tint.x);
	};

	for (uint32_t i = 1; i < ActiveGrassTypeCount(); ++i) {
		auto& type = resolvedGrassTypes.grassType[i];
		type.fuzzDirectionalAlbedoParams = GetFuzzDirectionalAlbedoParameters(type.grassSurfParams.w);

		// Anchor the scattering tint at the same area-weighted colour used to stabilize blade appearance.
		float4 referenceColor(
			std::lerp(type.baseColor.x, type.tipColor.x, 1.0f / 3.0f),
			std::lerp(type.baseColor.y, type.tipColor.y, 1.0f / 3.0f),
			std::lerp(type.baseColor.z, type.tipColor.z, 1.0f / 3.0f), 0.0f);

		if (prelinearizeTypeColors) {
			convertTint(type.grassSubsurfaceColor);
			convertTint(type.grassBounceColor);
			convertTint(referenceColor);
		}

		// Precompute the colour ratio on type updates; the pixel shader only multiplies to retain material detail.
		type.grassSubsurfaceColor.x /= std::max(referenceColor.x, 1e-4f);
		type.grassSubsurfaceColor.y /= std::max(referenceColor.y, 1e-4f);
		type.grassSubsurfaceColor.z /= std::max(referenceColor.z, 1e-4f);
	}

	float maxCurveReach = 0.0f;
	float maxNearWidth = 0.0f;
	float maxClumpPull = 0.0f;  // Largest grid size times pull factor, in world units.

	for (uint32_t i = 0; i < MaxGrassTypes; ++i) {
		const auto& source = resolvedGrassTypes.grassType[i];
		// Unused slots stay zeroed; give them a valid grid so a stray lookup cannot divide by zero.
		const float clumpGridSize = std::max(source.clumpGridSize, MinClumpGridSize);
		resolvedGeneratorTypes.grassType[i] = GrassGeneratorType{
			source.height, source.width, source.minSlope, source.maxSlope,
			source.stiffness, source.rotationalStiffness, source.tipWeight, source.mid,
			source.clumpDistanceFactor, source.clumpHeightFactor, source.clumpFacingFactor, source.clumpLeanFactor,
			clumpGridSize, 1.0f / clumpGridSize, source.grassSurfParams.z, 0.0f
		};

		const float maxBend = 1.85f * source.stiffness;
		const float controlReach = source.height * std::sqrt(source.mid * source.mid + maxBend * maxBend);
		maxCurveReach = std::max(maxCurveReach, std::max(source.height, controlReach));
		maxClumpPull = std::max(maxClumpPull, clumpGridSize * std::abs(source.clumpDistanceFactor));
		const float baseWidth = source.width * 2.5f * 1.3f;
		maxNearWidth = std::max(maxNearWidth, baseWidth * 2.0f);  // Low is the widest tier.
	}

	// Rebuild the presence map only when slope limits change.
	uint64_t slopeLimitsHash = GrassHashOffsetBasis;
	for (const auto& type : resolvedGeneratorTypes.grassType) {
		GrassHashValue(slopeLimitsHash, type.minSlope);
		GrassHashValue(slopeLimitsHash, type.maxSlope);
	}
	if (slopeLimitsHash != resolvedSlopeLimitsHash) {
		resolvedSlopeLimitsHash = slopeLimitsHash;
		grassPresenceContentHash = (std::numeric_limits<uint64_t>::max)();
	}

	grassTypesArrayCB->Update(resolvedGrassTypes);
	grassGeneratorTypesCB->Update(resolvedGeneratorTypes);

	// View thickening scales with blade width.
	nearQuadrantFrustumPadding = maxClumpPull + maxCurveReach + maxNearWidth * (1.0f + settings.grassViewThicken);
	lowFadeInPositionPadding = maxClumpPull * 0.1125f + 1.0f;
	hiZClumpReach = maxClumpPull * 0.16f;
	nearHiZRadius = maxCurveReach + maxNearWidth * (1.0f + settings.grassViewThicken) + hiZClumpReach + 1.0f;
	grassTypesDirty = false;
}
