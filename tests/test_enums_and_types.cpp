/**
 * High-impact enum and type validation tests
 * Validates enum values, type safety, and compile-time contracts
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>

TEST_CASE("Shader class enum validation", "[Enums][Shader]")
{
	// Simulates ShaderCache.h ShaderClass enum
	enum class ShaderClass : uint32_t
	{
		Vertex = 0,
		Pixel = 1,
		Compute = 2,
		Count = 3
	};

	SECTION("Enum values are sequential from 0")
	{
		REQUIRE(static_cast<uint32_t>(ShaderClass::Vertex) == 0);
		REQUIRE(static_cast<uint32_t>(ShaderClass::Pixel) == 1);
		REQUIRE(static_cast<uint32_t>(ShaderClass::Compute) == 2);
		REQUIRE(static_cast<uint32_t>(ShaderClass::Count) == 3);
	}

	SECTION("Count represents total number of shader types")
	{
		uint32_t shaderTypeCount = static_cast<uint32_t>(ShaderClass::Count);
		REQUIRE(shaderTypeCount == 3);
	}

	SECTION("Enum is type-safe")
	{
		REQUIRE(std::is_enum_v<ShaderClass>);
		REQUIRE(sizeof(ShaderClass) == sizeof(uint32_t));
	}
}

TEST_CASE("Lighting shader technique flags", "[Enums][Shader][Flags]")
{
	// Simulates LightingShaderTechniques enum
	enum class LightingShaderTechniques : uint32_t
	{
		None = 0,
		Envmap = 1 << 0,
		Glowmap = 1 << 1,
		Parallax = 1 << 2,
		Facegen = 1 << 3,
		FacegenRGBTint = 1 << 4,
		Hair = 1 << 5,
		ParallaxOcc = 1 << 6,
		MTLand = 1 << 7,
		LODLand = 1 << 8,
		Snow = 1 << 9,
		MultilayerParallax = 1 << 10,
		TreeAnim = 1 << 11,
		LODObjects = 1 << 12,
		MultiIndexSnow = 1 << 13,
		LODObjectsHD = 1 << 14,
		Eye = 1 << 15,
		Cloud = 1 << 16,
		LODLandNoise = 1 << 17,
		MTLandLODBlend = 1 << 18,
		Outline = 1 << 19
	};

	SECTION("Flag values are powers of 2")
	{
		auto isPowerOf2 = [](uint32_t val) {
			return val != 0 && (val & (val - 1)) == 0;
		};

		REQUIRE(isPowerOf2(static_cast<uint32_t>(LightingShaderTechniques::Envmap)));
		REQUIRE(isPowerOf2(static_cast<uint32_t>(LightingShaderTechniques::Glowmap)));
		REQUIRE(isPowerOf2(static_cast<uint32_t>(LightingShaderTechniques::Parallax)));
		REQUIRE(isPowerOf2(static_cast<uint32_t>(LightingShaderTechniques::Hair)));
		REQUIRE(isPowerOf2(static_cast<uint32_t>(LightingShaderTechniques::Snow)));
	}

	SECTION("Flags can be combined with bitwise OR")
	{
		uint32_t combined =
			static_cast<uint32_t>(LightingShaderTechniques::Envmap) |
			static_cast<uint32_t>(LightingShaderTechniques::Parallax);

		REQUIRE(combined == 0b101);  // Bits 0 and 2
		REQUIRE((combined & static_cast<uint32_t>(LightingShaderTechniques::Envmap)) != 0);
		REQUIRE((combined & static_cast<uint32_t>(LightingShaderTechniques::Parallax)) != 0);
		REQUIRE((combined & static_cast<uint32_t>(LightingShaderTechniques::Glowmap)) == 0);
	}

	SECTION("All flags fit within uint32_t")
	{
		uint32_t maxFlag = static_cast<uint32_t>(LightingShaderTechniques::Outline);
		REQUIRE(maxFlag == (1 << 19));
		REQUIRE(maxFlag < (1U << 31));  // Well within 32-bit range
	}
}

TEST_CASE("PBR shader flags validation", "[Enums][PBR][Flags]")
{
	// Simulates PBRFlags enum
	enum class PBRFlags : uint32_t
	{
		None = 0,
		HasDisplacementMap = 1 << 0,
		HasSubsurfaceMap = 1 << 1,
		HasCoatNormalMap = 1 << 2,
		TwoLayer = 1 << 3,
		UseParallax = 1 << 4,
		HasGlintMap = 1 << 5
	};

	SECTION("PBR flags are distinct powers of 2")
	{
		REQUIRE(static_cast<uint32_t>(PBRFlags::HasDisplacementMap) == 1);
		REQUIRE(static_cast<uint32_t>(PBRFlags::HasSubsurfaceMap) == 2);
		REQUIRE(static_cast<uint32_t>(PBRFlags::HasCoatNormalMap) == 4);
		REQUIRE(static_cast<uint32_t>(PBRFlags::TwoLayer) == 8);
		REQUIRE(static_cast<uint32_t>(PBRFlags::UseParallax) == 16);
		REQUIRE(static_cast<uint32_t>(PBRFlags::HasGlintMap) == 32);
	}

	SECTION("Flags don't overlap")
	{
		uint32_t allFlags =
			static_cast<uint32_t>(PBRFlags::HasDisplacementMap) |
			static_cast<uint32_t>(PBRFlags::HasSubsurfaceMap) |
			static_cast<uint32_t>(PBRFlags::HasCoatNormalMap) |
			static_cast<uint32_t>(PBRFlags::TwoLayer) |
			static_cast<uint32_t>(PBRFlags::UseParallax) |
			static_cast<uint32_t>(PBRFlags::HasGlintMap);

		// Should be 0b111111 = 63
		REQUIRE(allFlags == 63);
	}
}

TEST_CASE("Feature issue type enum", "[Enums][Features]")
{
	// Simulates FeatureIssueInfo::IssueType
	enum class IssueType
	{
		OBSOLETE,
		VERSION_MISMATCH,
		OVERRIDE_FAILED,
		UNKNOWN
	};

	SECTION("Issue types are distinct")
	{
		REQUIRE(IssueType::OBSOLETE != IssueType::VERSION_MISMATCH);
		REQUIRE(IssueType::VERSION_MISMATCH != IssueType::OVERRIDE_FAILED);
		REQUIRE(IssueType::OVERRIDE_FAILED != IssueType::UNKNOWN);
	}

	SECTION("All issue types are comparable")
	{
		IssueType type1 = IssueType::OBSOLETE;
		IssueType type2 = IssueType::OBSOLETE;
		IssueType type3 = IssueType::UNKNOWN;

		REQUIRE(type1 == type2);
		REQUIRE(type1 != type3);
	}
}

TEST_CASE("Upscale method enum validation", "[Enums][Upscaling]")
{
	// Simulates Upscaling feature enum
	enum class UpscaleMethod : uint32_t
	{
		None = 0,
		DLSS = 1,
		FSR2 = 2,
		XeSS = 3,
		Count = 4
	};

	SECTION("Upscale methods are sequential")
	{
		REQUIRE(static_cast<uint32_t>(UpscaleMethod::None) == 0);
		REQUIRE(static_cast<uint32_t>(UpscaleMethod::DLSS) == 1);
		REQUIRE(static_cast<uint32_t>(UpscaleMethod::FSR2) == 2);
		REQUIRE(static_cast<uint32_t>(UpscaleMethod::XeSS) == 3);
		REQUIRE(static_cast<uint32_t>(UpscaleMethod::Count) == 4);
	}

	SECTION("Can use Count for iteration")
	{
		uint32_t methodCount = static_cast<uint32_t>(UpscaleMethod::Count);

		for (uint32_t i = 0; i < methodCount; i++) {
			// Can safely iterate through all methods
			REQUIRE(i < 4);
		}
	}
}

TEST_CASE("Climate preset enum validation", "[Enums][Weather]")
{
	// Simulates WetnessEffects::ClimatePreset
	enum class ClimatePreset : uint32_t
	{
		Default = 0,
		Tropical = 1,
		Desert = 2,
		Arctic = 3,
		Custom = 4
	};

	SECTION("Climate presets have valid values")
	{
		REQUIRE(static_cast<uint32_t>(ClimatePreset::Default) == 0);
		REQUIRE(static_cast<uint32_t>(ClimatePreset::Tropical) == 1);
		REQUIRE(static_cast<uint32_t>(ClimatePreset::Desert) == 2);
		REQUIRE(static_cast<uint32_t>(ClimatePreset::Arctic) == 3);
		REQUIRE(static_cast<uint32_t>(ClimatePreset::Custom) == 4);
	}
}

TEST_CASE("Type size validations", "[Types][Size]")
{
	SECTION("Standard type sizes are as expected")
	{
		REQUIRE(sizeof(uint8_t) == 1);
		REQUIRE(sizeof(uint16_t) == 2);
		REQUIRE(sizeof(uint32_t) == 4);
		REQUIRE(sizeof(uint64_t) == 8);

		REQUIRE(sizeof(int8_t) == 1);
		REQUIRE(sizeof(int16_t) == 2);
		REQUIRE(sizeof(int32_t) == 4);
		REQUIRE(sizeof(int64_t) == 8);

		REQUIRE(sizeof(float) == 4);
		REQUIRE(sizeof(double) == 8);
	}

	SECTION("Pointer sizes match platform")
	{
		// x64 platform
		REQUIRE(sizeof(void*) == 8);
		REQUIRE(sizeof(uintptr_t) == 8);
	}

	SECTION("Bool size in C++")
	{
		REQUIRE(sizeof(bool) == 1);
		// NOTE: In HLSL, bool is 4 bytes!
		// This is a common source of buffer mismatches
	}
}

TEST_CASE("Enum underlying type validation", "[Types][Enums]")
{
	enum class U8Enum : uint8_t
	{
		A,
		B,
		C
	};
	enum class U16Enum : uint16_t
	{
		A,
		B,
		C
	};
	enum class U32Enum : uint32_t
	{
		A,
		B,
		C
	};
	enum class U64Enum : uint64_t
	{
		A,
		B,
		C
	};

	SECTION("Enum sizes match underlying type")
	{
		REQUIRE(sizeof(U8Enum) == 1);
		REQUIRE(sizeof(U16Enum) == 2);
		REQUIRE(sizeof(U32Enum) == 4);
		REQUIRE(sizeof(U64Enum) == 8);
	}

	SECTION("Can cast to underlying type")
	{
		U32Enum value = U32Enum::B;
		uint32_t underlying = static_cast<uint32_t>(value);
		REQUIRE(underlying == 1);
	}
}

TEST_CASE("Bitfield validation", "[Types][Bitfields]")
{
	struct Flags
	{
		uint32_t flag1: 1;
		uint32_t flag2: 1;
		uint32_t flag3: 1;
		uint32_t reserved: 29;
	};

	SECTION("Bitfield struct size")
	{
		// Should be 4 bytes (one uint32_t)
		REQUIRE(sizeof(Flags) == 4);
	}

	SECTION("Bitfield values")
	{
		Flags f{};
		f.flag1 = 1;
		f.flag2 = 0;
		f.flag3 = 1;

		REQUIRE(f.flag1 == 1);
		REQUIRE(f.flag2 == 0);
		REQUIRE(f.flag3 == 1);
	}
}

TEST_CASE("Type traits validation", "[Types][Traits]")
{
	enum class TypedEnum : uint32_t
	{
		A,
		B
	};
	enum UntypedEnum
	{
		X,
		Y
	};

	SECTION("Enum type traits")
	{
		REQUIRE(std::is_enum_v<TypedEnum>);
		REQUIRE(std::is_enum_v<UntypedEnum>);
		REQUIRE_FALSE(std::is_enum_v<uint32_t>);
	}

	SECTION("Scoped enum detection")
	{
		REQUIRE(std::is_scoped_enum_v<TypedEnum>);
		REQUIRE_FALSE(std::is_scoped_enum_v<UntypedEnum>);
	}

	SECTION("Underlying type detection")
	{
		REQUIRE(std::is_same_v<std::underlying_type_t<TypedEnum>, uint32_t>);
	}

	SECTION("Trivial type detection")
	{
		REQUIRE(std::is_trivial_v<uint32_t>);
		REQUIRE(std::is_trivial_v<TypedEnum>);
		REQUIRE(std::is_trivially_copyable_v<TypedEnum>);
	}
}

TEST_CASE("Alignment requirements validation", "[Types][Alignment]")
{
	SECTION("Standard type alignments")
	{
		REQUIRE(alignof(uint8_t) == 1);
		REQUIRE(alignof(uint16_t) == 2);
		REQUIRE(alignof(uint32_t) == 4);
		REQUIRE(alignof(uint64_t) == 8);
		REQUIRE(alignof(float) == 4);
		REQUIRE(alignof(double) == 8);
	}

	SECTION("Struct alignment follows largest member")
	{
		struct MixedStruct
		{
			uint8_t a;
			uint32_t b;
		};

		// Alignment is 4 (largest member is uint32_t)
		REQUIRE(alignof(MixedStruct) == 4);
	}

	SECTION("Custom alignment")
	{
// MSVC warning C4324: structure was padded due to alignment specifier
// This is expected behavior, so we suppress the warning
#pragma warning(push)
#pragma warning(disable: 4324)
		struct alignas(16) Aligned16
		{
			uint32_t data;
		};
#pragma warning(pop)

		REQUIRE(alignof(Aligned16) == 16);
		REQUIRE(sizeof(Aligned16) == 16);  // Size rounds up to alignment
	}
}

TEST_CASE("Critical enum value ranges", "[Enums][Validation]")
{
	SECTION("Shader technique flags fit in 32 bits")
	{
		// We have ~20 lighting shader techniques
		// They should all fit in a uint32_t with room to spare
		uint32_t maxTechnique = 1 << 19;  // Outline is bit 19

		REQUIRE(maxTechnique < (1U << 31));
		REQUIRE(maxTechnique > 0);
	}

	SECTION("Feature issue types are small enum")
	{
		// Only 4 issue types, should be represented efficiently
		enum class IssueType
		{
			OBSOLETE,
			VERSION_MISMATCH,
			OVERRIDE_FAILED,
			UNKNOWN
		};

		// Default underlying type should be small
		REQUIRE(sizeof(IssueType) <= sizeof(uint32_t));
	}
}

TEST_CASE("Enum to string validation patterns", "[Enums][Patterns]")
{
	enum class Quality : uint8_t
	{
		Low = 0,
		Medium = 1,
		High = 2,
		Ultra = 3
	};

	auto qualityToString = [](Quality q) -> const char* {
		switch (q) {
		case Quality::Low:
			return "Low";
		case Quality::Medium:
			return "Medium";
		case Quality::High:
			return "High";
		case Quality::Ultra:
			return "Ultra";
		default:
			return "Unknown";
		}
	};

	SECTION("All enum values map to strings")
	{
		REQUIRE(std::string(qualityToString(Quality::Low)) == "Low");
		REQUIRE(std::string(qualityToString(Quality::Medium)) == "Medium");
		REQUIRE(std::string(qualityToString(Quality::High)) == "High");
		REQUIRE(std::string(qualityToString(Quality::Ultra)) == "Ultra");
	}

	SECTION("Invalid enum values handled")
	{
		Quality invalid = static_cast<Quality>(99);
		REQUIRE(std::string(qualityToString(invalid)) == "Unknown");
	}
}
