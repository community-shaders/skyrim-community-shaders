#ifndef WATER_BRDF_HLSLI
#define WATER_BRDF_HLSLI

namespace WaterBRDF
{
	// ============================================================================
	// PHYSICALLY-BASED WATER SPECULAR (Cook-Torrance Microfacet BRDF)
	// Based on Google Filament's material system
	// Reference: https://google.github.io/filament/Filament.md.html
	//
	// Standard Cook-Torrance specular BRDF:
	// f_r(v,l) = D(h,α) * V(v,l,α) * F(v,h,f0)
	//
	// Where:
	// - D: GGX Normal Distribution Function (microfacet orientation)
	// - V: Smith-GGX Visibility Function = G / (4 * NdotV * NdotL) (self-shadowing)
	// - F: Schlick Fresnel Approximation (surface reflection at angles)
	//
	// For water:
	// - F0 = 0.02 (IOR 1.33: ((1.33-1)/(1.33+1))^2 = 0.02)
	// - Very low roughness for calm water (α ≈ 0.001-0.05)
	// ============================================================================

	// Water F0 constant: IOR 1.33 → F0 = 0.02 (achromatic for dielectrics)
	static const float3 WATER_F0 = float3(0.02f, 0.02f, 0.02f);

	// Artistic energy boost for sun path appearance matching reference photos
	// Real-world water exhibits stronger visible specular due to wave statistics
	// and multiple scattering not captured by single-layer BRDF
	static const float WATER_SPECULAR_ENERGY_BOOST = 2.5f;

	/**
	 * Converts specular power to roughness parameter
	 * @param specPower Specular power (higher = sharper reflection)
	 * @return Roughness parameter (α) for BRDF calculations
	 */
	float SpecularPowerToRoughness(float specPower)
	{
		// Convert specular power to perceptual roughness
		// Higher power = sharper reflection = lower roughness
		// Standard conversion: perceptualRoughness = sqrt(2/(n+2))
		float perceptualRoughness = saturate(sqrt(2.0f / (specPower + 2.0f)));

		// Clamp minimum roughness to prevent numerical issues
		// Filament recommends 0.045 for fp32, but we use slightly lower for water
		// Water can be extremely smooth, but not perfectly mirror-like
		perceptualRoughness = max(perceptualRoughness, 0.001f);

		// Square for roughness (α in Filament terminology)
		return perceptualRoughness * perceptualRoughness;
	}

	/**
	 * GGX Normal Distribution Function (optimized Filament implementation)
	 * @param NdotH Dot product of normal and half-vector
	 * @param roughness Roughness parameter (α)
	 * @return Distribution value
	 */
	float D_GGX(float NdotH, float roughness)
	{
		// ============================================================================
		// D: GGX Normal Distribution Function
		// ============================================================================
		// Filament's optimized GGX implementation
		// D_GGX(h,α) = α² / (π * ((n·h)² * (α² - 1) + 1)²)
		//
		// This can be rewritten for better numerical stability:
		// D = (α² / π) * (1 / ((n·h)² * (α² - 1) + 1)²)
		float a = NdotH * roughness;
		float k = roughness / (1.0f - NdotH * NdotH + a * a);
		float D = k * k * (1.0f / Math::PI);
		return D;
	}

	/**
	 * Smith-GGX Height-Correlated Visibility Function (optimized Filament implementation)
	 * @param NdotV Dot product of normal and view direction
	 * @param NdotL Dot product of normal and light direction
	 * @param roughness Roughness parameter (α)
	 * @return Visibility value
	 */
	float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
	{
		// ============================================================================
		// V: Smith-GGX Height-Correlated Visibility Function
		// ============================================================================
		// Filament's optimized approximation (accurate and fast):
		// V(v,l,α) = 0.5 / (NdotL * (NdotV * (1-α) + α) + NdotV * (NdotL * (1-α) + α))
		//
		// This combines:
		// - Geometric shadowing G(v,l,α)
		// - Denominator normalization (4 * NdotV * NdotL)
		// into a single "visibility" term for efficiency
		float GGXV = NdotL * (NdotV * (1.0f - roughness) + roughness);
		float GGXL = NdotV * (NdotL * (1.0f - roughness) + roughness);
		return 0.5f / max(GGXV + GGXL, 1e-6f);
	}

	/**
	 * Schlick Fresnel Approximation (optimized Filament implementation)
	 * @param VdotH Dot product of view direction and half-vector
	 * @param f0 Fresnel reflectance at normal incidence
	 * @return Fresnel term
	 */
	float3 F_Schlick(float VdotH, float3 f0)
	{
		// ============================================================================
		// F: Schlick Fresnel Approximation
		// ============================================================================
		// F_Schlick(v,h,F0,F90) = F0 + (F90 - F0) * (1 - v·h)^5
		//
		// Optimized form from Filament (Listing 6) with F90=1 for dielectrics:
		// F_Schlick(u,F0) = F0 + (1 - F0) * (1 - u)^5
		//                 = f + F0 * (1 - f)  where f = (1 - u)^5
		//
		// This refactoring is more efficient for scalar operations and avoids
		// unnecessary vector arithmetic when F90 = 1.0 (achromatic at grazing angles)
		//
		// Physical meaning:
		// - At normal incidence (perpendicular): reflection = F0 = 2%
		// - At grazing angles (parallel): reflection → F90 = 100%
		// Water becomes mirror-like at shallow viewing angles (Fresnel effect)
		float f = pow(1.0f - VdotH, 5.0f);
		return f + f0 * (1.0f - f);
	}

	/**
	 * Cook-Torrance specular BRDF for water surfaces
	 * @param normal Surface normal
	 * @param viewDirection View direction (towards camera)
	 * @param lightDirection Light direction (towards light)
	 * @param specPower Specular power parameter
	 * @return Specular BRDF value
	 */
	float3 SpecularBRDF(float3 normal, float3 viewDirection, float3 lightDirection, float specPower)
	{
		// Calculate vectors
		float3 V = -viewDirection;  // View direction (to camera)
		float3 L = lightDirection;  // Light direction (to light)
		float3 H = normalize(L + V);  // Half vector

		// Calculate dot products
		float NdotH = saturate(dot(normal, H));
		float NdotV = max(dot(normal, V), 1e-4f);  // Clamp to avoid division by zero
		float NdotL = saturate(dot(normal, L));
		float VdotH = saturate(dot(V, H));

		// Early exit if sun is below horizon or facing away
		if (NdotL < 1e-4f)
			return 0.0.xxx;

		// Convert specular power to roughness
		float roughness = SpecularPowerToRoughness(specPower);

		// Calculate BRDF components
		float D = D_GGX(NdotH, roughness);
		float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
		float3 F = F_Schlick(VdotH, WATER_F0);

		// Combine: Specular BRDF = D * Vis * F
		// Note: The 4 * NdotV * NdotL denominator is already in the Vis term
		return D * Vis * F;
	}

	/**
	 * Calculate sun specular contribution for water surfaces
	 * @param normal Surface normal
	 * @param viewDirection View direction (towards camera)
	 * @param sunDir Sun direction (xyz) and intensity (w)
	 * @param sunColor Sun color (xyz)
	 * @param specPower Specular power parameter
	 * @param turbidity Turbidity/scatter factor (higher = clearer water)
	 * @return Sun specular color contribution
	 */
	float3 GetSunSpecular(float3 normal, float3 viewDirection, float4 sunDir, float3 sunColor, float specPower, float turbidity)
	{
		// Calculate BRDF
		float3 specularBRDF = SpecularBRDF(normal, viewDirection, sunDir.xyz, specPower);

		// Calculate NdotL for energy conservation
		float3 L = sunDir.xyz;
		float3 V = -viewDirection;
		float NdotL = saturate(dot(normal, L));

		// === Energy Conservation ===
		// The specular BRDF should be multiplied by NdotL for the rendering equation
		// L_out = f_r * L_in * NdotL
		// 
		// turbidity = scatter factor (mod authors control via .esp)
		//             - Higher values = brighter specular (clearer water)
		//             - Lower values = dimmer specular (murky/turbid water)
		float3 sunSpecular = specularBRDF * sunColor * sunDir.w * turbidity * NdotL;

		// Apply artistic energy boost
		sunSpecular *= WATER_SPECULAR_ENERGY_BOOST;

		return sunSpecular;
	}
}

#endif  // WATER_BRDF_HLSLI
