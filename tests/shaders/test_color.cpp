// Tests for Common/Color.hlsli
#include "test_helpers_unified.h"

// Test RGB to luminance conversion
SHADER_TEST_SIMPLE("Color.hlsli - RGBToLuminance", "[color][luminance]",
	"/Shaders/Tests/TestColor.hlsl", "TestRGBToLuminance")

// Test RGB/YCoCg color space roundtrip conversion
SHADER_TEST_SIMPLE("Color.hlsli - RGB/YCoCg Roundtrip", "[color][colorspace]",
	"/Shaders/Tests/TestColor.hlsl", "TestRGBYCoCgRoundtrip")

// Test saturation adjustment
SHADER_TEST_SIMPLE("Color.hlsli - Saturation", "[color][saturation]",
	"/Shaders/Tests/TestColor.hlsl", "TestSaturation")

// Test gamma space conversion roundtrip
SHADER_TEST_SIMPLE("Color.hlsli - Gamma Conversion Roundtrip", "[color][gamma]",
	"/Shaders/Tests/TestColor.hlsl", "TestGammaConversionRoundtrip")

// Test multi-bounce ambient occlusion
SHADER_TEST_SIMPLE("Color.hlsli - MultiBounceAO", "[color][ao]",
	"/Shaders/Tests/TestColor.hlsl", "TestMultiBounceAO")

// Test Lagarde specular AO
SHADER_TEST_SIMPLE("Color.hlsli - SpecularAOLagarde", "[color][ao]",
	"/Shaders/Tests/TestColor.hlsl", "TestSpecularAOLagarde")

// Test different luminance calculation variants
SHADER_TEST_SIMPLE("Color.hlsli - RGB to Luminance Variants", "[color][luminance]",
	"/Shaders/Tests/TestColor.hlsl", "TestRGBToLuminanceVariants")

// Test diffuse and light helper functions
SHADER_TEST_SIMPLE("Color.hlsli - Diffuse and Light", "[color][conversion]",
	"/Shaders/Tests/TestColor.hlsl", "TestDiffuseAndLight")
