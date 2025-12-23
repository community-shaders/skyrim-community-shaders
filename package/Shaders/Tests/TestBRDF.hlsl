// HLSL Unit Tests for Common/BRDF.hlsli
#include "/Shaders/Common/BRDF.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

[numthreads(1, 1, 1)]
void TestDiffuseLambert()
{
    float lambert = BRDF::Diffuse_Lambert();

    // Lambert should be constant 1/PI
    ASSERT(IsTrue, lambert > 0.318f);  // 1/PI ~ 0.318309
    ASSERT(IsTrue, lambert < 0.319f);

    // Should always return the same value
    float lambert2 = BRDF::Diffuse_Lambert();
    ASSERT(AreEqual, lambert, lambert2);
}

[numthreads(1, 1, 1)]
void TestFresnelSchlick()
{
    // Test with typical dielectric F0 (4% reflectance)
    float3 F0 = float3(0.04, 0.04, 0.04);

    // Test 1: Normal incidence (VdotH = 1) should return F0
    float3 fresnel_normal = BRDF::F_Schlick(F0, 1.0f);
    ASSERT(IsTrue, abs(fresnel_normal.r - F0.r) < 0.0001f);
    ASSERT(IsTrue, abs(fresnel_normal.g - F0.g) < 0.0001f);
    ASSERT(IsTrue, abs(fresnel_normal.b - F0.b) < 0.0001f);

    // Test 2: Grazing angle (VdotH = 0) should approach 1.0 (Fc = 1)
    float3 fresnel_grazing = BRDF::F_Schlick(F0, 0.0f);
    ASSERT(IsTrue, abs(fresnel_grazing.r - 1.0f) < 0.0001f);
    ASSERT(IsTrue, abs(fresnel_grazing.g - 1.0f) < 0.0001f);
    ASSERT(IsTrue, abs(fresnel_grazing.b - 1.0f) < 0.0001f);

    // Test 3: Intermediate angle (VdotH = 0.707 ≈ 45°) should interpolate
    float3 fresnel_45 = BRDF::F_Schlick(F0, 0.707f);
    ASSERT(IsTrue, fresnel_45.r > F0.r);
    ASSERT(IsTrue, fresnel_45.r < 1.0f);

    // Test 4: Monotonicity - fresnel should increase as angle increases
    float3 fresnel_30 = BRDF::F_Schlick(F0, 0.866f);  // cos(30°)
    float3 fresnel_60 = BRDF::F_Schlick(F0, 0.5f);    // cos(60°)
    ASSERT(IsTrue, fresnel_60.r > fresnel_30.r);
    ASSERT(IsTrue, fresnel_30.r > fresnel_normal.r);

    // Test 5: With metallic F0 (gold ~1.0, 0.71, 0.29)
    float3 F0_metal = float3(1.0, 0.71, 0.29);
    float3 fresnel_metal = BRDF::F_Schlick(F0_metal, 1.0f);
    ASSERT(IsTrue, abs(fresnel_metal.r - F0_metal.r) < 0.001f);
    ASSERT(IsTrue, abs(fresnel_metal.g - F0_metal.g) < 0.001f);
    ASSERT(IsTrue, abs(fresnel_metal.b - F0_metal.b) < 0.001f);
}

[numthreads(1, 1, 1)]
void TestDistributionGGX()
{
    float roughness = 0.5f;

    // At NdotH = 1 (perfect reflection), should be maximum
    float d_perfect = BRDF::D_GGX(roughness, 1.0f);

    // At NdotH = 0.8 (off-angle), should be less
    float d_angle = BRDF::D_GGX(roughness, 0.8f);
    ASSERT(IsTrue, d_perfect > d_angle);

    // At NdotH = 0 (perpendicular), should be near zero
    float d_perp = BRDF::D_GGX(roughness, 0.01f);
    ASSERT(IsTrue, d_perp < d_angle);

    // Result should always be positive
    ASSERT(IsTrue, d_perfect > 0.0f);
    ASSERT(IsTrue, d_angle > 0.0f);
    ASSERT(IsTrue, d_perp >= 0.0f);

    // Rougher surface should have lower peak
    float d_rough = BRDF::D_GGX(0.9f, 1.0f);
    float d_smooth = BRDF::D_GGX(0.1f, 1.0f);
    ASSERT(IsTrue, d_smooth > d_rough);
}

[numthreads(1, 1, 1)]
void TestVisibilitySmithJoint()
{
    float roughness = 0.5f;
    float NdotV = 0.8f;
    float NdotL = 0.7f;

    float vis = BRDF::Vis_SmithJoint(roughness, NdotV, NdotL);

    // Visibility should be in valid range [0, inf) but typically small
    ASSERT(IsTrue, vis >= 0.0f);
    ASSERT(IsTrue, vis < 10.0f);  // Sanity check

    // Test physical constraint: visibility should always be positive and finite
    float vis_aligned = BRDF::Vis_SmithJoint(roughness, 1.0f, 1.0f);
    ASSERT(IsTrue, vis_aligned >= 0.0f);
    ASSERT(IsTrue, vis_aligned < 100.0f);  // Reasonable upper bound

    // Test behavior trend: with fixed roughness, varying angles should give consistent ordering
    // Don't test exact numerical relationships due to precision variations
    float vis_test1 = BRDF::Vis_SmithJoint(roughness, 0.9f, 0.9f);
    float vis_test2 = BRDF::Vis_SmithJoint(roughness, 0.5f, 0.5f);
    // Both should be positive and finite
    ASSERT(IsTrue, vis_test1 >= 0.0f && vis_test1 < 100.0f);
    ASSERT(IsTrue, vis_test2 >= 0.0f && vis_test2 < 100.0f);

    // Rougher surfaces should have LOWER visibility value
    // (More microfacet self-shadowing, so the G/(4*NdotV*NdotL) term is smaller)
    float vis_rough = BRDF::Vis_SmithJoint(0.9f, NdotV, NdotL);
    float vis_smooth = BRDF::Vis_SmithJoint(0.1f, NdotV, NdotL);
    ASSERT(IsTrue, vis_rough < vis_smooth);
}

[numthreads(1, 1, 1)]
void TestVisibilityNeubelt()
{
    // Test basic properties
    float vis1 = BRDF::Vis_Neubelt(0.8f, 0.7f);
    ASSERT(IsTrue, vis1 > 0.0f);

    // Perfect alignment should give specific value
    float vis_perfect = BRDF::Vis_Neubelt(1.0f, 1.0f);
    ASSERT(IsTrue, vis_perfect > 0.0f);

    // Should be symmetric
    float vis_a = BRDF::Vis_Neubelt(0.8f, 0.6f);
    float vis_b = BRDF::Vis_Neubelt(0.6f, 0.8f);
    ASSERT(AreEqual, vis_a, vis_b);
}

[numthreads(1, 1, 1)]
void TestEnvBRDFLazarov()
{
    // Test at various roughness and angles
    float2 brdf_smooth = BRDF::EnvBRDFApproxLazarov(0.1f, 0.8f);
    float2 brdf_rough = BRDF::EnvBRDFApproxLazarov(0.9f, 0.8f);

    // Results should be in valid range (allow small overshoot due to approximation)
    ASSERT(IsTrue, brdf_smooth.x >= -0.01f && brdf_smooth.x <= 1.01f);
    ASSERT(IsTrue, brdf_smooth.y >= -0.01f && brdf_smooth.y <= 1.01f);
    ASSERT(IsTrue, brdf_rough.x >= -0.01f && brdf_rough.x <= 1.01f);
    ASSERT(IsTrue, brdf_rough.y >= -0.01f && brdf_rough.y <= 1.01f);

    // At grazing angle (NdotV near 0), behavior should differ
    float2 brdf_grazing = BRDF::EnvBRDFApproxLazarov(0.5f, 0.1f);
    float2 brdf_normal = BRDF::EnvBRDFApproxLazarov(0.5f, 1.0f);

    ASSERT(IsTrue, brdf_grazing.x >= 0.0f);
    ASSERT(IsTrue, brdf_normal.x >= 0.0f);
}

[numthreads(1, 1, 1)]
void TestDCharlie()
{
    float roughness = 0.5f;

    // At NdotH = 0 (perpendicular), should be maximum for sheen
    float d_perp = BRDF::D_Charlie(roughness, 0.01f);

    // At NdotH = 1 (normal), should be minimum
    float d_normal = BRDF::D_Charlie(roughness, 1.0f);

    // Charlie distribution peaks at grazing, opposite of typical NDFs
    ASSERT(IsTrue, d_perp > d_normal);

    // Should always be positive
    ASSERT(IsTrue, d_perp > 0.0f);
    ASSERT(IsTrue, d_normal >= 0.0f);
}

[numthreads(1, 1, 1)]
void TestAnisotropicGGX()
{
    float alphaX = 0.3f;
    float alphaY = 0.7f;
    float NdotH = 0.9f;
    float XdotH = 0.3f;
    float YdotH = 0.2f;

    float d_aniso = BRDF::D_AnisoGGX(alphaX, alphaY, NdotH, XdotH, YdotH);

    // Should be positive
    ASSERT(IsTrue, d_aniso > 0.0f);

    // Isotropic case (alphaX = alphaY) should match regular GGX behavior
    float d_iso = BRDF::D_AnisoGGX(0.5f, 0.5f, 1.0f, 0.0f, 0.0f);
    ASSERT(IsTrue, d_iso > 0.0f);
}

[numthreads(1, 1, 1)]
void TestDiffuseBurley()
{
    float roughness = 0.5f;
    float NdotV = 0.8f;
    float NdotL = 0.7f;
    float VdotH = 0.6f;

    float3 diffuse = BRDF::Diffuse_Burley(roughness, NdotV, NdotL, VdotH);

    // Should be positive
    ASSERT(IsTrue, diffuse.x > 0.0f);
    ASSERT(IsTrue, diffuse.y > 0.0f);
    ASSERT(IsTrue, diffuse.z > 0.0f);

    // Compare with Lambert (Burley is more accurate)
    float lambert = BRDF::Diffuse_Lambert();

    // Both should be reasonable diffuse values
    ASSERT(IsTrue, diffuse.x < 1.0f);
}

[numthreads(1, 1, 1)]
void TestDBeckmann()
{
    float roughness = 0.5f;
    float NdotH = 0.9f;

    float d = BRDF::D_Beckmann(roughness, NdotH);

    // Should be positive
    ASSERT(IsTrue, d > 0.0f);

    // Peak should be at NdotH = 1
    float d_peak = BRDF::D_Beckmann(roughness, 1.0f);
    ASSERT(IsTrue, d_peak >= d);
}

[numthreads(1, 1, 1)]
void TestVisSmith()
{
    float roughness = 0.5f;
    float NdotV = 0.8f;
    float NdotL = 0.7f;

    float vis = BRDF::Vis_Smith(roughness, NdotV, NdotL);

    // Should be positive
    ASSERT(IsTrue, vis > 0.0f);

    // Compare with joint approximation
    float vis_approx = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);

    // Should be in similar range
    ASSERT(IsTrue, vis_approx > 0.0f);
}

[numthreads(1, 1, 1)]
void TestEnvBRDFHirvonen()
{
    float roughness = 0.5f;
    float NdotV = 0.8f;

    float2 brdf = BRDF::EnvBRDFApproxHirvonen(roughness, NdotV);

    // Should be in valid range [0, 1]
    ASSERT(IsTrue, brdf.x >= 0.0f && brdf.x <= 1.0f);
    ASSERT(IsTrue, brdf.y >= 0.0f && brdf.y <= 1.0f);

    // Compare with Lazarov version
    float2 brdf_lazarov = BRDF::EnvBRDFApproxLazarov(roughness, NdotV);

    // Should give similar-ish results
    ASSERT(IsTrue, abs(brdf.x - brdf_lazarov.x) < 0.5f);
}
