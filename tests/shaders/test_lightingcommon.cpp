// Tests for Common/LightingCommon.hlsli
#include "test_helpers_unified.h"

// Shininess to Roughness
SHADER_TEST_SIMPLE("LightingCommon - Shininess to Roughness", "[lightingcommon][conversion]",
	"/Shaders/Tests/TestLightingCommon.hlsl", "TestShininessToRoughness")
