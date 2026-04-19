// HLSL Unit Tests for Common/GBuffer.hlsli
#include "/Shaders/Common/GBuffer.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestNormalEncodingRoundtrip() {
	half3 testNormals[6] = {
		half3(0.01h, 0.0h, 1.0h),    // near +Z pole
		half3(0.0h, 0.01h, -1.0h),   // near -Z pole
		half3(1.0h, 0.0h, 0.0h),
		half3(-1.0h, 0.0h, 0.0h),
		half3(0.0h, 1.0h, 0.0h),
		half3(0.0h, -1.0h, 0.0h)
	};

	for (int i = 0; i < 6; i++) {
		half3 original = normalize(testNormals[i]);
		half2 encoded = GBuffer::EncodeNormal(original);
		half3 decoded = GBuffer::DecodeNormal(encoded);

		ASSERT(IsTrue, abs(decoded.x - original.x) < 0.05h);
		ASSERT(IsTrue, abs(decoded.y - original.y) < 0.05h);
		ASSERT(IsTrue, abs(decoded.z - original.z) < 0.05h);
	}
}

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestNormalEncodingAngledNormals() {
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

		half length = sqrt(decoded.x * decoded.x + decoded.y * decoded.y + decoded.z * decoded.z);
		ASSERT(IsTrue, abs(length - 1.0h) < 0.05h);
	}
}

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestNormalEncodingEquator() {
	half3 equatorNormal = half3(1.0h, 0.0h, 0.0h);
	half2 encoded = GBuffer::EncodeNormal(equatorNormal);
	half3 decoded = GBuffer::DecodeNormal(encoded);

	ASSERT(IsTrue, abs(decoded.x - 1.0h) < 0.01h);
	ASSERT(IsTrue, abs(decoded.y) < 0.01h);
	ASSERT(IsTrue, abs(decoded.z) < 0.01h);
}
