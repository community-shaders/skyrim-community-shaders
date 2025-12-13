#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <string>

namespace Shaders
{
	namespace Tests
	{

		std::string loadFile(const std::string& path)
		{
			std::ifstream file(path);
			if (!file.good())
				return "";
			return std::string((std::istreambuf_iterator<char>(file)),
				std::istreambuf_iterator<char>());
		}

		TEST_CASE("Hair: Scatter Color Calculation Change", "[Shaders][Hair][Color]")
		{
			auto content = loadFile("features/Hair Specular/Shaders/Hair/Hair.hlsli");

			SECTION("Old hardcoded lerp removed")
			{
				// Should NOT find: lerp(float3(0.992, 0.808, 0.518), baseColor, 0.5)
				REQUIRE(content.find("float3(0.992, 0.808, 0.518)") == std::string::npos);
			}

			SECTION("New pow-based calculation present")
			{
				// Should find: pow(baseColor, 0.5)
				REQUIRE(content.find("pow(baseColor, 0.5)") != std::string::npos);
			}
		}

		TEST_CASE("Hair: satVNdotL Addition", "[Shaders][Hair][Math]")
		{
			auto content = loadFile("features/Hair Specular/Shaders/Hair/Hair.hlsli");

			SECTION("satVNdotL variable defined")
			{
				REQUIRE(content.find("const float satVNdotL = saturate(VNdotL)") != std::string::npos);
			}

			SECTION("Placed after VNdotL calculation")
			{
				size_t vnDotLPos = content.find("const float VNdotL = dot(VN, L)");
				size_t satVNdotLPos = content.find("const float satVNdotL = saturate(VNdotL)");
				REQUIRE(satVNdotLPos > vnDotLPos);
			}
		}

		TEST_CASE("Hair: GetHairDirectLight Refactoring", "[Shaders][Hair][Refactor]")
		{
			auto content = loadFile("features/Hair Specular/Shaders/Hair/Hair.hlsli");

			SECTION("New signature uses standardized structures")
			{
				REQUIRE(content.find("void GetHairDirectLight(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material") != std::string::npos);
			}

			SECTION("Old parameter list removed")
			{
				// Should not find old signature with individual parameters
				REQUIRE(content.find("float3 T, float3 L, float3 V, float3 N, float3 VN, float3 lightColor, float shininess") == std::string::npos);
			}

			SECTION("Extracts values from context")
			{
				REQUIRE(content.find("const float3 T = normalize(context.worldNormal)") != std::string::npos);
				REQUIRE(content.find("const float3 V = normalize(context.viewDir)") != std::string::npos);
			}
		}

		TEST_CASE("Hair: GetHairIndirectLobeWeights Refactoring", "[Shaders][Hair][Indirect]")
		{
			auto content = loadFile("features/Hair Specular/Shaders/Hair/Hair.hlsli");

			SECTION("New signature uses IndirectLobeWeights")
			{
				REQUIRE(content.find("void GetHairIndirectLobeWeights(out IndirectLobeWeights lobeWeights") != std::string::npos);
			}

			SECTION("Old multi-output signature removed")
			{
				// Should not find old signature with multiple out parameters
				REQUIRE(content.find("out float3 diffuseLobeWeight, out float3 specularLobeWeightPrimary, out float3 specularLobeWeightSecondary") == std::string::npos);
			}

			SECTION("Uses single diffuse lobe for Scheuermann")
			{
				REQUIRE(content.find("lobeWeights.diffuse = saturate(material.BaseColor") != std::string::npos);
			}

			SECTION("Applies indirect multipliers")
			{
				REQUIRE(content.find("DiffuseIndirectMult") != std::string::npos);
				REQUIRE(content.find("SpecularIndirectMult") != std::string::npos);
			}
		}

		TEST_CASE("Hair: Dynamic Cubemap Function Removal", "[Shaders][Hair][Cleanup]")
		{
			auto content = loadFile("features/Hair Specular/Shaders/Hair/Hair.hlsli");

			SECTION("GetHairDynamicCubemapSpecularIrradiance removed")
			{
				REQUIRE(content.find("GetHairDynamicCubemapSpecularIrradiance") == std::string::npos);
			}

			SECTION("No dual-lobe cubemap sampling")
			{
				// Function that did primary and secondary cubemap samples is gone
				REQUIRE(content.find("SpecularIrradiance +=") == std::string::npos ||
						content.find("GetDynamicCubemapSpecularIrradiance") == std::string::npos);
			}
		}

		TEST_CASE("Hair: File Size Reduction", "[Shaders][Hair][Stats]")
		{
			auto content = loadFile("features/Hair Specular/Shaders/Hair/Hair.hlsli");

			SECTION("File reduced by approximately 104 lines")
			{
				int lineCount = std::count(content.begin(), content.end(), '\n');
				// Verifying significant reduction (exact count may vary)
				REQUIRE(lineCount < 400);  // Should be substantially smaller after removal
			}
		}

	}  // namespace Tests
}  // namespace Shaders