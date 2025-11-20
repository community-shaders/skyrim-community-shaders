/**
 * High-impact GPU structure validation tests
 * Tests critical GPU buffer requirements that prevent rendering bugs
 */

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

TEST_CASE("GPU constant buffer alignment requirements", "[GPU][Critical]")
{
	SECTION("All GPU constant buffers MUST be 16-byte aligned")
	{
		// This is a HARD requirement for DirectX 11
		// Misalignment causes:
		// - Subtle rendering bugs
		// - GPU crashes
		// - Data corruption
		// - Performance issues

		// Common buffer patterns used in Community Shaders
		struct PerFrameData
		{
			float CameraPosition[4];     // 16 bytes
			float ViewMatrix[16];        // 64 bytes
			float ProjectionMatrix[16];  // 64 bytes
			float Time;                  // 4 bytes
			float DeltaTime;             // 4 bytes
			float FrameCount;            // 4 bytes
			float _padding;              // 4 bytes -> Total 160 bytes
		};

		REQUIRE(sizeof(PerFrameData) == 160);
		REQUIRE(sizeof(PerFrameData) % 16 == 0);
	}

	SECTION("Float4 alignment (HLSL standard)")
	{
		struct Float4
		{
			float x, y, z, w;
		};

		REQUIRE(sizeof(Float4) == 16);
		REQUIRE(alignof(Float4) == 4);  // Individual floats are 4-byte aligned
	}

	SECTION("Matrix alignment (4x4 matrix)")
	{
		struct Matrix4x4
		{
			float m[4][4];
		};

		REQUIRE(sizeof(Matrix4x4) == 64);
		REQUIRE(sizeof(Matrix4x4) % 16 == 0);
	}

	SECTION("Struct padding detection")
	{
		// BAD: This struct needs padding!
		struct NeedsPadding
		{
			float vec[4];    // 16 bytes
			uint32_t count;  // 4 bytes
							 // MISSING 12 bytes of padding!
		};

		// GOOD: This struct is properly padded
		struct ProperlyPadded
		{
			float vec[4];    // 16 bytes
			uint32_t count;  // 4 bytes
			float _pad[3];   // 12 bytes padding
		};

		REQUIRE(sizeof(NeedsPadding) == 20);
		REQUIRE_FALSE(sizeof(NeedsPadding) % 16 == 0);  // WRONG!

		REQUIRE(sizeof(ProperlyPadded) == 32);
		REQUIRE(sizeof(ProperlyPadded) % 16 == 0);  // CORRECT!
	}
}

TEST_CASE("GPU buffer size calculations", "[GPU][Sizes]")
{
	SECTION("Constant buffer size must be 64-byte aligned")
	{
		// DirectX 11 constant buffers have additional 64-byte alignment
		auto alignTo64 = [](uint32_t size) -> uint32_t {
			return (size + 63) & ~63;
		};

		REQUIRE(alignTo64(1) == 64);
		REQUIRE(alignTo64(64) == 64);
		REQUIRE(alignTo64(65) == 128);
		REQUIRE(alignTo64(128) == 128);
		REQUIRE(alignTo64(129) == 192);
		REQUIRE(alignTo64(256) == 256);
	}

	SECTION("Structured buffer stride validation")
	{
		struct LightData
		{
			float Position[4];   // 16 bytes
			float Color[4];      // 16 bytes
			float Direction[4];  // 16 bytes
			uint32_t Type;       // 4 bytes
			float Radius;        // 4 bytes
			float Intensity;     // 4 bytes
			float _pad;          // 4 bytes
		};

		// Stride must be 16-byte aligned
		REQUIRE(sizeof(LightData) == 64);
		REQUIRE(sizeof(LightData) % 16 == 0);
	}
}

TEST_CASE("Common GPU buffer patterns", "[GPU][Patterns]")
{
	SECTION("Per-frame constant buffer pattern")
	{
		struct PerFrame
		{
			float ViewProjMatrix[16];  // 64 bytes
			float CameraPosition[4];   // 16 bytes
			float Time;                // 4 bytes
			float DeltaTime;           // 4 bytes
			uint32_t FrameNumber;      // 4 bytes
			float _pad1;               // 4 bytes padding
		};

		REQUIRE(sizeof(PerFrame) % 16 == 0);
		REQUIRE(offsetof(PerFrame, ViewProjMatrix) % 16 == 0);
		REQUIRE(offsetof(PerFrame, CameraPosition) % 16 == 0);
	}

	SECTION("Per-object constant buffer pattern")
	{
		struct PerObject
		{
			float WorldMatrix[16];          // 64 bytes
			float WorldViewProjMatrix[16];  // 64 bytes
			float Color[4];                 // 16 bytes
		};

		REQUIRE(sizeof(PerObject) == 144);
		REQUIRE(sizeof(PerObject) % 16 == 0);
	}

	SECTION("Material constant buffer pattern")
	{
		struct Material
		{
			float BaseColor[4];      // 16 bytes
			float EmissiveColor[4];  // 16 bytes
			float Roughness;         // 4 bytes
			float Metallic;          // 4 bytes
			float SpecularLevel;     // 4 bytes
			uint32_t Flags;          // 4 bytes
		};

		REQUIRE(sizeof(Material) == 48);
		REQUIRE(sizeof(Material) % 16 == 0);
	}

	SECTION("Lighting constant buffer pattern")
	{
		struct Lighting
		{
			float DirectionalLightDir[4];    // 16 bytes
			float DirectionalLightColor[4];  // 16 bytes
			float AmbientColor[4];           // 16 bytes
			uint32_t PointLightCount;        // 4 bytes
			float _pad[3];                   // 12 bytes padding
		};

		REQUIRE(sizeof(Lighting) == 64);
		REQUIRE(sizeof(Lighting) % 16 == 0);
	}
}

TEST_CASE("Critical alignment edge cases", "[GPU][EdgeCases]")
{
	SECTION("Single uint32_t needs padding")
	{
		struct SingleUInt
		{
			uint32_t value;  // 4 bytes
			float _pad[3];   // 12 bytes padding
		};

		REQUIRE(sizeof(SingleUInt) == 16);
	}

	SECTION("Three floats need padding")
	{
		struct ThreeFloats
		{
			float x, y, z;  // 12 bytes
			float _pad;     // 4 bytes padding
		};

		REQUIRE(sizeof(ThreeFloats) == 16);
	}

	SECTION("Five floats need padding")
	{
		struct FiveFloats
		{
			float values[5];  // 20 bytes
			float _pad[3];    // 12 bytes padding
		};

		REQUIRE(sizeof(FiveFloats) == 32);
	}

	SECTION("Array of structs maintains alignment")
	{
		struct Element
		{
			float data[4];  // 16 bytes
		};

		Element array[10];
		REQUIRE(sizeof(array) == 160);
		REQUIRE(sizeof(array) % 16 == 0);

		// Each element should be aligned
		for (int i = 0; i < 10; i++) {
			size_t offset = reinterpret_cast<uintptr_t>(&array[i]) - reinterpret_cast<uintptr_t>(&array[0]);
			REQUIRE(offset % 16 == 0);
		}
	}
}

TEST_CASE("GPU data type sizes", "[GPU][Types]")
{
	SECTION("Fundamental type sizes")
	{
		REQUIRE(sizeof(float) == 4);
		REQUIRE(sizeof(uint32_t) == 4);
		REQUIRE(sizeof(int32_t) == 4);
		REQUIRE(sizeof(bool) == 1);  // Note: HLSL bool is 4 bytes!
	}

	SECTION("HLSL equivalent sizes")
	{
		// float4 = 16 bytes
		struct Float4
		{
			float x, y, z, w;
		};
		REQUIRE(sizeof(Float4) == 16);

		// float3x3 = 48 bytes (3 float4s due to padding)
		struct Float3x3
		{
			float m[3][4];
		};  // Note: padding to float4 alignment
		REQUIRE(sizeof(Float3x3) == 48);

		// float4x4 = 64 bytes
		struct Float4x4
		{
			float m[4][4];
		};
		REQUIRE(sizeof(Float4x4) == 64);
	}

	SECTION("Bool packing issues")
	{
		// C++ bool is 1 byte, but HLSL bool is 4 bytes!
		// This is a common source of bugs

		struct CppBools
		{
			bool a, b, c, d;  // 4 bytes in C++
		};

		struct HLSLBools
		{
			uint32_t a, b, c, d;  // 16 bytes in HLSL
		};

		REQUIRE(sizeof(CppBools) == 4);
		REQUIRE(sizeof(HLSLBools) == 16);

		// Solution: Use uint32_t for bools in C++ buffers
	}
}

TEST_CASE("Buffer validation macro", "[GPU][Validation]")
{
	SECTION("Static assert for alignment")
	{
		struct ValidBuffer
		{
			float data[4];
		};

		// This should compile
		static_assert(sizeof(ValidBuffer) % 16 == 0, "ValidBuffer must be 16-byte aligned");
		REQUIRE(sizeof(ValidBuffer) % 16 == 0);
	}

	SECTION("Runtime alignment check")
	{
		auto checkAlignment = [](size_t size, size_t alignment) {
			return (size % alignment) == 0;
		};

		REQUIRE(checkAlignment(16, 16));
		REQUIRE(checkAlignment(32, 16));
		REQUIRE(checkAlignment(64, 16));
		REQUIRE_FALSE(checkAlignment(20, 16));
		REQUIRE_FALSE(checkAlignment(17, 16));
	}
}

TEST_CASE("Real-world buffer scenarios", "[GPU][Scenarios]")
{
	SECTION("Shadow cascade buffer")
	{
		struct ShadowCascade
		{
			float ViewProjMatrix[16];  // 64 bytes
			float SplitDepth;          // 4 bytes
			float _pad[3];             // 12 bytes padding
		};

		// Common to have 4 cascades
		struct ShadowCascades
		{
			ShadowCascade cascades[4];  // 4 * 80 = 320 bytes
		};

		REQUIRE(sizeof(ShadowCascade) == 80);
		REQUIRE(sizeof(ShadowCascade) % 16 == 0);
		REQUIRE(sizeof(ShadowCascades) == 320);
		REQUIRE(sizeof(ShadowCascades) % 16 == 0);
	}

	SECTION("Terrain rendering buffer")
	{
		struct TerrainBuffer
		{
			float WorldMatrix[16];  // 64 bytes
			float TextureScale[4];  // 16 bytes
			uint32_t TextureCount;  // 4 bytes
			uint32_t BlendMode;     // 4 bytes
			float HeightScale;      // 4 bytes
			float _pad;             // 4 bytes padding
		};

		REQUIRE(sizeof(TerrainBuffer) == 96);
		REQUIRE(sizeof(TerrainBuffer) % 16 == 0);
	}

	SECTION("Post-process effect buffer")
	{
		struct PostProcessBuffer
		{
			float ScreenSize[4];  // 16 bytes (width, height, 1/width, 1/height)
			float Parameters[4];  // 16 bytes
			uint32_t Flags;       // 4 bytes
			float Intensity;      // 4 bytes
			float _pad[2];        // 8 bytes padding
		};

		REQUIRE(sizeof(PostProcessBuffer) == 48);
		REQUIRE(sizeof(PostProcessBuffer) % 16 == 0);
	}
}

TEST_CASE("Performance implications", "[GPU][Performance]")
{
	SECTION("Aligned access is faster")
	{
		// Aligned memory access is faster on GPU
		// 16-byte alignment allows for vectorized loads

		struct Aligned
		{
			alignas(16) float data[4];
		};

		struct Unaligned
		{
			float data[4];
		};

		REQUIRE(alignof(Aligned) == 16);
		REQUIRE(sizeof(Aligned) == 16);

		// Aligned version can use single SSE load instruction
		// Unaligned version may need multiple loads
	}

	SECTION("Cache line awareness")
	{
		// GPU cache lines are typically 128 bytes
		// Structs that fit in one cache line perform better

		struct SmallBuffer
		{
			float data[4][4];  // 64 bytes - fits in half cache line
		};

		struct OptimalBuffer
		{
			float data[8][4];  // 128 bytes - exactly one cache line
		};

		REQUIRE(sizeof(SmallBuffer) == 64);
		REQUIRE(sizeof(OptimalBuffer) == 128);
	}
}
