// Tests for Common/BRDF.hlsli
#include "test_helpers_unified.h"

// Diffuse Lambert
SHADER_TEST_SIMPLE("BRDF - Diffuse Lambert", "[brdf][diffuse]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestDiffuseLambert")

// Fresnel Schlick
SHADER_TEST_SIMPLE("BRDF - Fresnel Schlick", "[brdf][fresnel]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestFresnelSchlick")

// Distribution GGX
SHADER_TEST_SIMPLE("BRDF - Distribution GGX", "[brdf][ndf]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestDistributionGGX")

// Visibility Smith Joint
SHADER_TEST_SIMPLE("BRDF - Visibility Smith Joint", "[brdf][visibility]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestVisibilitySmithJoint")

// Visibility Neubelt
SHADER_TEST_SIMPLE("BRDF - Visibility Neubelt", "[brdf][visibility]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestVisibilityNeubelt")

// Environment BRDF Lazarov
SHADER_TEST_SIMPLE("BRDF - Environment BRDF Lazarov", "[brdf][ibl]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestEnvBRDFLazarov")

// Distribution Charlie (Sheen)
SHADER_TEST_SIMPLE("BRDF - Distribution Charlie (Sheen)", "[brdf][ndf][sheen]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestDCharlie")

// Anisotropic GGX
SHADER_TEST_SIMPLE("BRDF - Anisotropic GGX", "[brdf][ndf][anisotropic]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestAnisotropicGGX")

// Diffuse Burley
SHADER_TEST_SIMPLE("BRDF - Diffuse Burley", "[brdf][diffuse]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestDiffuseBurley")

// Distribution Beckmann
SHADER_TEST_SIMPLE("BRDF - Distribution Beckmann", "[brdf][ndf]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestDBeckmann")

// Visibility Smith
SHADER_TEST_SIMPLE("BRDF - Visibility Smith", "[brdf][visibility]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestVisSmith")

// Environment BRDF Hirvonen
SHADER_TEST_SIMPLE("BRDF - Environment BRDF Hirvonen", "[brdf][ibl]",
	"/Shaders/Tests/TestBRDF.hlsl", "TestEnvBRDFHirvonen")
