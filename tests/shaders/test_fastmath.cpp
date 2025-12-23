// Tests for Common/FastMath.hlsli
#include "test_helpers_unified.h"

// Fast RcpSqrt NR0
SHADER_TEST_SIMPLE("FastMath - Fast RcpSqrt NR0", "[fastmath][rsqrt]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastRcpSqrtNR0")

// Fast RcpSqrt NR1
SHADER_TEST_SIMPLE("FastMath - Fast RcpSqrt NR1", "[fastmath][rsqrt]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastRcpSqrtNR1")

// Fast RcpSqrt NR2
SHADER_TEST_SIMPLE("FastMath - Fast RcpSqrt NR2", "[fastmath][rsqrt]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastRcpSqrtNR2")

// Fast Sqrt NR0
SHADER_TEST_SIMPLE("FastMath - Fast Sqrt NR0", "[fastmath][sqrt]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastSqrtNR0")

// Fast Sqrt NR1
SHADER_TEST_SIMPLE("FastMath - Fast Sqrt NR1", "[fastmath][sqrt]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastSqrtNR1")

// Fast Sqrt NR2
SHADER_TEST_SIMPLE("FastMath - Fast Sqrt NR2", "[fastmath][sqrt]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastSqrtNR2")

// Fast Rcp NR0
SHADER_TEST_SIMPLE("FastMath - Fast Rcp NR0", "[fastmath][reciprocal]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastRcpNR0")

// Fast Rcp NR1
SHADER_TEST_SIMPLE("FastMath - Fast Rcp NR1", "[fastmath][reciprocal]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastRcpNR1")

// Fast Rcp NR2
SHADER_TEST_SIMPLE("FastMath - Fast Rcp NR2", "[fastmath][reciprocal]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestFastRcpNR2")

// acosFast4
SHADER_TEST_SIMPLE("FastMath - acosFast4", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestAcosFast4")

// asinFast4
SHADER_TEST_SIMPLE("FastMath - asinFast4", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestAsinFast4")

// atanFast4
SHADER_TEST_SIMPLE("FastMath - atanFast4", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestAtanFast4")

// ACos
SHADER_TEST_SIMPLE("FastMath - ACos", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestACos")

// ASin
SHADER_TEST_SIMPLE("FastMath - ASin", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestASin")

// ATanPos
SHADER_TEST_SIMPLE("FastMath - ATanPos", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestATanPos")

// ATan
SHADER_TEST_SIMPLE("FastMath - ATan", "[fastmath][trig]",
	"/Shaders/Tests/TestFastMath.hlsl", "TestATan")
