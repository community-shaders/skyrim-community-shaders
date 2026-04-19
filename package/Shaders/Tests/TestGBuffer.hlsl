// HLSL Unit Tests for Common/GBuffer.hlsli
#include "/Shaders/Common/GBuffer.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestNormalEncodingRoundtrip() {
	float3 testNormals[5] = {
		float3(0.3, 0.0, 1.0),  // near +Z
		float3(1.0, 0.0, 0.0),
		float3(-1.0, 0.0, 0.0),
		float3(0.0, 1.0, 0.0),
		float3(0.0, -1.0, 0.0)
	};

	for (int i = 0; i < 5; i++) {
		float3 original = normalize(testNormals[i]);
		float2 encoded = GBuffer::EncodeNormal(original);
		float3 decoded = GBuffer::DecodeNormal(encoded);

		ASSERT(IsTrue, abs(decoded.x - original.x) < 0.05);
		ASSERT(IsTrue, abs(decoded.y - original.y) < 0.05);
		ASSERT(IsTrue, abs(decoded.z - original.z) < 0.05);
	}
}

	/// @tags gbuffer, normal, encoding
	[numthreads(1, 1, 1)] void TestNormalEncodingAngledNormals()
{
	float3 testNormals[4] = {
		normalize(float3(1.0, 1.0, 1.0)),
		normalize(float3(-1.0, 1.0, 1.0)),
		normalize(float3(1.0, -1.0, 1.0)),
		normalize(float3(1.0, 1.0, -1.0))
	};

	for (int i = 0; i < 4; i++) {
		float3 original = testNormals[i];
		float2 encoded = GBuffer::EncodeNormal(original);
		float3 decoded = GBuffer::DecodeNormal(encoded);

		float length = sqrt(decoded.x * decoded.x + decoded.y * decoded.y + decoded.z * decoded.z);
		ASSERT(IsTrue, abs(length - 1.0) < 0.05);
	}
}

/// @tags gbuffer, normal, encoding
[numthreads(1, 1, 1)] void TestNormalEncodingEquator() {
	float3 equatorNormal = float3(1.0, 0.0, 0.0);
	float2 encoded = GBuffer::EncodeNormal(equatorNormal);
	float3 decoded = GBuffer::DecodeNormal(encoded);

	ASSERT(IsTrue, abs(decoded.x - 1.0) < 0.01);
	ASSERT(IsTrue, abs(decoded.y) < 0.01);
	ASSERT(IsTrue, abs(decoded.z) < 0.01);
}
