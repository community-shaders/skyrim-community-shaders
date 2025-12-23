// Tests for Common/DisplayMapping.hlsli
#include "test_helpers_unified.h"

// RangeCompress Single
SHADER_TEST_SIMPLE("DisplayMapping - RangeCompress Single", "[displaymapping][compression]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRangeCompressSingle")

// RangeCompress Threshold
SHADER_TEST_SIMPLE("DisplayMapping - RangeCompress Threshold", "[displaymapping][compression]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRangeCompressThreshold")

// RangeCompress Float3
SHADER_TEST_SIMPLE("DisplayMapping - RangeCompress Float3", "[displaymapping][compression]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRangeCompressFloat3")

// Linear to PQ Roundtrip
SHADER_TEST_SIMPLE("DisplayMapping - Linear to PQ Roundtrip", "[displaymapping][pq][hdr]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestLinearToPQRoundtrip")

// RGB to XYZ Roundtrip
SHADER_TEST_SIMPLE("DisplayMapping - RGB to XYZ Roundtrip", "[displaymapping][colorspace]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRGBToXYZRoundtrip")

// XYZ to LMS Roundtrip
SHADER_TEST_SIMPLE("DisplayMapping - XYZ to LMS Roundtrip", "[displaymapping][colorspace]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestXYZToLMSRoundtrip")

// RGB to ICtCp Roundtrip
SHADER_TEST_SIMPLE("DisplayMapping - RGB to ICtCp Roundtrip", "[displaymapping][colorspace][ictcp]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRGBToICtCpRoundtrip")

// ICtCp Luminance
SHADER_TEST_SIMPLE("DisplayMapping - ICtCp Luminance", "[displaymapping][ictcp]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestICtCpLuminance")

// PQ Constants
SHADER_TEST_SIMPLE("DisplayMapping - PQ Constants", "[displaymapping][pq]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestPQConstants")

// RGB to XYZ White Point
SHADER_TEST_SIMPLE("DisplayMapping - RGB to XYZ White Point", "[displaymapping][colorspace]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRGBToXYZWhitePoint")

// RGB to XYZ Black
SHADER_TEST_SIMPLE("DisplayMapping - RGB to XYZ Black", "[displaymapping][colorspace]",
	"/Shaders/Tests/TestDisplayMapping.hlsl", "TestRGBToXYZBlack")
