// Tests for Common/GBuffer.hlsli
#include "test_helpers_unified.h"

// Test octahedral normal encoding roundtrip for cardinal directions
SHADER_TEST_SIMPLE("GBuffer - Normal Encoding Roundtrip", "[gbuffer][encoding]",
	"/Shaders/Tests/TestGBuffer.hlsl", "TestNormalEncodingRoundtrip")

// Test octahedral normal encoding behavioral properties for diagonal/angled normals
SHADER_TEST_SIMPLE("GBuffer - Normal Encoding Angled Normals", "[gbuffer][encoding]",
	"/Shaders/Tests/TestGBuffer.hlsl", "TestNormalEncodingAngledNormals")

// Test OctWrap helper function behavioral properties
SHADER_TEST_SIMPLE("GBuffer - OctWrap", "[gbuffer][encoding]",
	"/Shaders/Tests/TestGBuffer.hlsl", "TestOctWrap")

// Test vanilla (legacy) normal encoding
SHADER_TEST_SIMPLE("GBuffer - Vanilla Normal Encoding", "[gbuffer][encoding]",
	"/Shaders/Tests/TestGBuffer.hlsl", "TestVanillaNormalEncoding")
