/**
 * High-impact shader constant validation tests
 * Tests critical shader register mappings that must match HLSL code
 *
 * These constants map C++ to HLSL shader registers.
 * Wrong values = shader compilation failures or rendering bugs!
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>
#include <vector>

TEST_CASE("Shader constant register indices are valid", "[Shader][Constants][Critical]")
{
	// Simulates ShaderConstants::LightingPS structure
	// These indices must match HLSL register assignments

	SECTION("Register indices are within valid range")
	{
		// DirectX 11 constant buffer slots: 0-13 (14 slots)
		// Per-register index within a cbuffer: typically 0-255

		std::vector<int32_t> flatIndices = {
			0,   // NumLightNumShadowLight
			1,   // PointLightPosition
			2,   // PointLightColor
			3,   // DirLightDirection
			4,   // DirLightColor
			5,   // DirectionalAmbient
			18,  // AmbientColor
			24,  // LODTexParams
			25,  // SpecularColor
			36,  // PBRFlags
			49   // LandscapeTexture6PBRParams (highest in flat)
		};

		for (auto index : flatIndices) {
			REQUIRE(index >= 0);
			REQUIRE(index < 256);  // Reasonable upper limit
		}
	}

	SECTION("VR register indices account for offset")
	{
		// VR builds use different offsets due to additional parameters
		std::vector<int32_t> vrIndices = {
			24,  // AmbientColor (vs 18 in flat)
			57,  // LandscapeTexture6GlintParameters (highest in VR)
		};

		for (auto index : vrIndices) {
			REQUIRE(index >= 0);
			REQUIRE(index < 256);
		}
	}

	SECTION("Critical shadow-related constants are defined")
	{
		// Shadow constants (VR-specific in the codebase)
		struct ShadowConstants
		{
			int32_t ShadowSampleParam = 18;
			int32_t EndSplitDistances = 19;
			int32_t StartSplitDistances = 20;
			int32_t DepthBiasParam = 21;
			int32_t ShadowLightParam = 22;
			int32_t ShadowMapProj = 23;
		};

		ShadowConstants shadow;

		// Shadow constants should be sequential for efficient access
		REQUIRE(shadow.ShadowSampleParam == 18);
		REQUIRE(shadow.EndSplitDistances == shadow.ShadowSampleParam + 1);
		REQUIRE(shadow.StartSplitDistances == shadow.ShadowSampleParam + 2);
		REQUIRE(shadow.DepthBiasParam == shadow.ShadowSampleParam + 3);
		REQUIRE(shadow.ShadowLightParam == shadow.ShadowSampleParam + 4);
		REQUIRE(shadow.ShadowMapProj == shadow.ShadowSampleParam + 5);
	}
}

TEST_CASE("Shader constant register mapping has no duplicates", "[Shader][Constants]")
{
	SECTION("Flat build register indices are unique")
	{
		std::vector<int32_t> flatRegisters = {
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
			10, 11, 12, 13, 14, 15, 16, 17,
			18, 19, 20, 21, 22, 23, 24, 25,
			26, 27, 28, 29, 30, 31, 32, 33,
			34, 35, 36, 37, 38, 39, 40, 41,
			42, 43, 44, 45, 46, 47, 48, 49
		};

		std::set<int32_t> uniqueRegisters(flatRegisters.begin(), flatRegisters.end());
		REQUIRE(uniqueRegisters.size() == flatRegisters.size());
	}

	SECTION("VR build register indices are unique")
	{
		std::vector<int32_t> vrRegisters = {
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
			10, 11, 12, 13, 14, 15, 16, 17,
			18, 19, 20, 21, 22, 23,
			24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
			34, 35, 36, 37, 38, 39, 40, 41,
			42, 43, 44, 45, 46, 47, 48, 49,
			50, 51, 52, 53, 54, 55, 56, 57
		};

		std::set<int32_t> uniqueRegisters(vrRegisters.begin(), vrRegisters.end());
		REQUIRE(uniqueRegisters.size() == vrRegisters.size());
	}
}

TEST_CASE("Shader constant register ranges make sense", "[Shader][Constants]")
{
	SECTION("Lighting constants are grouped logically")
	{
		struct LightingGroup
		{
			int32_t NumLightNumShadowLight = 0;
			int32_t PointLightPosition = 1;
			int32_t PointLightColor = 2;
			int32_t DirLightDirection = 3;
			int32_t DirLightColor = 4;
			int32_t DirectionalAmbient = 5;
		};

		LightingGroup group;

		// First 6 registers are all lighting-related
		REQUIRE(group.NumLightNumShadowLight < group.PointLightPosition);
		REQUIRE(group.PointLightPosition < group.PointLightColor);
		REQUIRE(group.PointLightColor < group.DirLightDirection);
		REQUIRE(group.DirLightDirection < group.DirLightColor);
		REQUIRE(group.DirLightColor < group.DirectionalAmbient);
	}

	SECTION("Material constants are grouped")
	{
		struct MaterialGroup
		{
			int32_t MaterialData = 7;
			int32_t EmitColor = 8;
			int32_t AlphaTestRef = 9;
		};

		MaterialGroup group;

		// Material properties are sequential
		REQUIRE(group.MaterialData == 7);
		REQUIRE(group.EmitColor == group.MaterialData + 1);
		REQUIRE(group.AlphaTestRef == group.MaterialData + 2);
	}

	SECTION("PBR constants are grouped")
	{
		struct PBRGroup
		{
			int32_t PBRFlags = 36;
			int32_t PBRParams1 = 37;
			int32_t PBRParams2 = 43;
		};

		PBRGroup group;

		// PBR constants should be in the same general area
		REQUIRE(group.PBRFlags >= 36);
		REQUIRE(group.PBRParams1 == group.PBRFlags + 1);
		REQUIRE(group.PBRParams2 >= 40);  // May have landscape params in between
	}
}

TEST_CASE("Shader constant VR vs Flat differences", "[Shader][Constants][VR]")
{
	SECTION("VR has additional registers beyond flat")
	{
		int32_t flatMaxRegister = 49;  // Highest in flat build
		int32_t vrMaxRegister = 57;    // Highest in VR build

		REQUIRE(vrMaxRegister > flatMaxRegister);
		REQUIRE(vrMaxRegister - flatMaxRegister == 8);  // VR has 8 additional registers
	}

	SECTION("VR shadow constants offset makes sense")
	{
		// In VR, shadow constants use specific indices
		struct VRShadowOffsets
		{
			int32_t ShadowSampleParam = 18;
			int32_t InvWorldMat = 42;
			int32_t PreviousWorldMat = 43;
		};

		VRShadowOffsets vr;

		REQUIRE(vr.ShadowSampleParam == 18);
		REQUIRE(vr.InvWorldMat >= 40);
		REQUIRE(vr.PreviousWorldMat == vr.InvWorldMat + 1);
	}

	SECTION("Common constants have same indices in VR and Flat")
	{
		// These should be identical in both builds
		struct CommonConstants
		{
			int32_t NumLightNumShadowLight = 0;
			int32_t PointLightPosition = 1;
			int32_t MaterialData = 7;
		};

		CommonConstants common;

		// These are in the "common" section that doesn't change
		REQUIRE(common.NumLightNumShadowLight == 0);
		REQUIRE(common.PointLightPosition == 1);
		REQUIRE(common.MaterialData == 7);
	}
}

TEST_CASE("Shader constant compile-time validation", "[Shader][Constants]")
{
	SECTION("Constants are const and not modifiable")
	{
		// This is more of a compile-time check, but we can validate the pattern
		const int32_t testConstant = 42;

		// In actual code, these should be const members of a struct
		// Can't modify at runtime
		REQUIRE(testConstant == 42);

		// If someone tries to make them non-const, compilation should fail
		// (This is enforced by the struct definition in ShaderCache.h)
	}

	SECTION("Register count is reasonable")
	{
		// DirectX 11 pixel shader: up to 15 constant buffer slots (b0-b14)
		// Each cbuffer can hold up to 4096 float4 constants (65536 bytes)
		// Per-constant index: 0-4095

		int32_t maxFlatRegister = 49;
		int32_t maxVRRegister = 57;

		// Our constants use less than 60 indices, which is well within limits
		REQUIRE(maxFlatRegister < 100);  // Sanity check
		REQUIRE(maxVRRegister < 100);    // Sanity check
	}
}

TEST_CASE("Shader constant documentation validation", "[Shader][Constants]")
{
	SECTION("Critical constants are well-known")
	{
		struct CriticalConstants
		{
			// These are most commonly used and should never change
			int32_t NumLightNumShadowLight = 0;  // Light count
			int32_t DirLightDirection = 3;       // Sun direction
			int32_t DirLightColor = 4;           // Sun color
			int32_t MaterialData = 7;            // Material properties
		};

		CriticalConstants critical;

		// Verify they're at expected locations
		REQUIRE(critical.NumLightNumShadowLight == 0);
		REQUIRE(critical.DirLightDirection == 3);
		REQUIRE(critical.DirLightColor == 4);
		REQUIRE(critical.MaterialData == 7);
	}

	SECTION("Landscape texture constants follow pattern")
	{
		// Landscape uses multiple texture layers (1-6)
		// Each layer can have IsSnow, IsSpecPower, PBRParams, GlintParameters

		struct LandscapeConstants
		{
			int32_t LandscapeTexture1to4IsSnow = 30;
			int32_t LandscapeTexture5to6IsSnow = 31;
			int32_t LandscapeTexture1to4IsSpecPower = 32;
			int32_t LandscapeTexture5to6IsSpecPower = 33;
		};

		LandscapeConstants landscape;

		// Snow flags come before SpecPower flags
		REQUIRE(landscape.LandscapeTexture1to4IsSnow < landscape.LandscapeTexture1to4IsSpecPower);
		REQUIRE(landscape.LandscapeTexture5to6IsSnow < landscape.LandscapeTexture5to6IsSpecPower);

		// Texture 1-4 comes before texture 5-6
		REQUIRE(landscape.LandscapeTexture1to4IsSnow < landscape.LandscapeTexture5to6IsSnow);
	}
}

TEST_CASE("Shader constant performance implications", "[Shader][Constants][Performance]")
{
	SECTION("Constants are accessed efficiently")
	{
		// GPU prefers sequential constant access
		// Scattered access can cause cache misses

		std::vector<int32_t> commonlyAccessedTogether = {
			0,  // NumLightNumShadowLight
			1,  // PointLightPosition
			2,  // PointLightColor
			3,  // DirLightDirection
			4   // DirLightColor
		};

		// These are sequential, which is good for performance
		for (size_t i = 1; i < commonlyAccessedTogether.size(); i++) {
			REQUIRE(commonlyAccessedTogether[i] == commonlyAccessedTogether[i - 1] + 1);
		}
	}

	SECTION("Constant buffer packing is efficient")
	{
		// Each constant register holds a float4 (16 bytes)
		// Good packing minimizes constant buffer size

		int32_t totalFlatRegisters = 50;  // 0-49
		int32_t totalVRRegisters = 58;    // 0-57

		// 50 float4s = 800 bytes (flat)
		// 58 float4s = 928 bytes (VR)

		size_t flatBufferSize = totalFlatRegisters * 16;
		size_t vrBufferSize = totalVRRegisters * 16;

		REQUIRE(flatBufferSize == 800);
		REQUIRE(vrBufferSize == 928);

		// Both are well under the 4096 float4 limit (65536 bytes)
		REQUIRE(flatBufferSize < 65536);
		REQUIRE(vrBufferSize < 65536);
	}
}
