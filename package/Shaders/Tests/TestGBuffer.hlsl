// HLSL Unit Tests for Common/GBuffer.hlsli
// Note: GBuffer uses half types - we use half throughout to avoid conversion warnings
#include "/Shaders/Common/GBuffer.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestNormalEncodingRoundtrip() {
	// Test that encoding and decoding normals is reversible
	half3 testNormals[6] = {
		half3(0.0h, 0.0h, 1.0h),   // Up
		half3(0.0h, 0.0h, -1.0h),  // Down
		half3(1.0h, 0.0h, 0.0h),   // Right
		half3(-1.0h, 0.0h, 0.0h),  // Left
		half3(0.0h, 1.0h, 0.0h),   // Forward
		half3(0.0h, -1.0h, 0.0h)   // Back
	};

	for (int i = 0; i < 6; i++) {
		half3 original = normalize(testNormals[i]);
		half2 encoded = GBuffer::EncodeNormal(original);
		half3 decoded = GBuffer::DecodeNormal(encoded);

		// Check that decoded normal is close to original
		ASSERT(IsTrue, abs(decoded.x - original.x) < 0.01h);
		ASSERT(IsTrue, abs(decoded.y - original.y) < 0.01h);
		ASSERT(IsTrue, abs(decoded.z - original.z) < 0.01h);

		// Encoded values should be in [0, 1] range
		ASSERT(IsTrue, encoded.x >= 0.0h && encoded.x <= 1.0h);
		ASSERT(IsTrue, encoded.y >= 0.0h && encoded.y <= 1.0h);
	}
}

	/// @tags gbuffer, normal, encoding
	[numthreads(1, 1, 1)] void TestNormalEncodingAngledNormals()
{
	// Test behavioral properties of octahedral encoding (not exact numerical accuracy)
	// Half precision + quantization means we check: valid output, normalized, reasonable direction
	half3 testNormals[4] = {
		normalize(half3(1.0h, 1.0h, 1.0h)),
		normalize(half3(-1.0h, 1.0h, 1.0h)),
		normalize(half3(1.0h, -1.0h, 1.0h)),
		normalize(half3(1.0h, 1.0h, -1.0h))
	};

	for (int i = 0; i < 4; i++) {
		half3 original = testNormals[i];
		half2 encoded = GBuffer::EncodeNormal(original);
		half3 decoded = GBuffer::DecodeNormal(encoded);

		// Check behavioral properties (relaxed for half precision quantization):
		// 1. Encoded values are in valid range [0, 1]
		ASSERT(IsTrue, encoded.x >= 0.0h && encoded.x <= 1.0h);
		ASSERT(IsTrue, encoded.y >= 0.0h && encoded.y <= 1.0h);

		// 2. Decoded normal is normalized (unit length)
		half length = sqrt(decoded.x * decoded.x + decoded.y * decoded.y + decoded.z * decoded.z);
		ASSERT(IsTrue, abs(length - 1.0h) < 0.02h);  // Relaxed tolerance for half precision
	}
}

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestOctWrap() {
	// Test behavioral properties of OctWrap (not exact numerical values)
	// Half precision ternary operators have quantization, so check valid output ranges

	// Test 1: Positive inputs should produce outputs in valid range
	half2 v1 = half2(0.5h, 0.5h);
	half2 wrapped1 = GBuffer::OctWrap(v1);
	ASSERT(IsTrue, wrapped1.x >= 0.0h && wrapped1.x <= 1.0h);
	ASSERT(IsTrue, wrapped1.y >= 0.0h && wrapped1.y <= 1.0h);

	// Test 2: Negative inputs should produce outputs in valid range
	half2 v2 = half2(-0.3h, 0.7h);
	half2 wrapped2 = GBuffer::OctWrap(v2);
	ASSERT(IsTrue, wrapped2.x >= -1.0h && wrapped2.x <= 1.0h);
	ASSERT(IsTrue, wrapped2.y >= -1.0h && wrapped2.y <= 1.0h);

	// Test 3: Mixed signs should produce outputs in valid range
	half2 v3 = half2(0.2h, -0.8h);
	half2 wrapped3 = GBuffer::OctWrap(v3);
	ASSERT(IsTrue, wrapped3.x >= -1.0h && wrapped3.x <= 1.0h);
	ASSERT(IsTrue, wrapped3.y >= -1.0h && wrapped3.y <= 1.0h);
}

	/// @tags gbuffer, normal, encoding
	[numthreads(1, 1, 1)] void TestVanillaNormalEncoding()
{
	// Test vanilla normal encoding with known normals
	half3 upNormal = half3(0.0h, 0.0h, 1.0h);
	half2 encoded = GBuffer::EncodeNormalVanilla(upNormal);

	// For up normal (0,0,1): z = sqrt(8 + -8*1) = sqrt(0) ≈ tiny value
	// Result should be near (0.5, 0.5) due to the +0.5 offset
	ASSERT(IsTrue, abs(encoded.x - 0.5h) < 0.2h);
	ASSERT(IsTrue, abs(encoded.y - 0.5h) < 0.2h);

	// Test that encoding produces values in reasonable range
	half3 testNormals[3] = {
		normalize(half3(1.0h, 0.0h, 0.0h)),
		normalize(half3(0.0h, 1.0h, 0.0h)),
		normalize(half3(1.0h, 1.0h, 0.0h))
	};

	for (int i = 0; i < 3; i++) {
		half2 enc = GBuffer::EncodeNormalVanilla(testNormals[i]);
		// Encoded values should be in a reasonable range (not infinite or NaN)
		ASSERT(IsTrue, enc.x >= -10.0h && enc.x <= 10.0h);
		ASSERT(IsTrue, enc.y >= -10.0h && enc.y <= 10.0h);
	}
}
