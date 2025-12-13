#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <string>
#include <regex>

// Validation tests for new LightingCommon.hlsli file
// These tests verify the structure and content of the shader file

namespace Shaders {
namespace Tests {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) return "";
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

TEST_CASE("LightingCommon: File Existence", "[Shaders][LightingCommon]") {
    SECTION("LightingCommon.hlsli exists at correct path") {
        std::ifstream file("package/Shaders/Common/LightingCommon.hlsli");
        REQUIRE(file.good());
    }
}

TEST_CASE("LightingCommon: Include Guards", "[Shaders][LightingCommon][Structure]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("Has proper include guard") {
        REQUIRE(content.find("#ifndef LIGHTING_COMMON_HLSLI") != std::string::npos);
        REQUIRE(content.find("#define LIGHTING_COMMON_HLSLI") != std::string::npos);
        REQUIRE(content.find("#endif") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: DirectContext Structure", "[Shaders][LightingCommon][Structs]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("DirectContext struct defined") {
        REQUIRE(content.find("struct DirectContext") != std::string::npos);
    }
    
    SECTION("Contains core lighting vectors") {
        REQUIRE(content.find("float3 worldNormal") != std::string::npos);
        REQUIRE(content.find("float3 vertexNormal") != std::string::npos);
        REQUIRE(content.find("float3 viewDir") != std::string::npos);
        REQUIRE(content.find("float3 lightDir") != std::string::npos);
        REQUIRE(content.find("float3 halfVector") != std::string::npos);
        REQUIRE(content.find("float3 lightColor") != std::string::npos);
    }
    
    SECTION("PBR-specific fields conditionally included") {
        REQUIRE(content.find("#if defined(TRUE_PBR)") != std::string::npos);
        REQUIRE(content.find("float3 coatWorldNormal") != std::string::npos);
        REQUIRE(content.find("float3 coatLightColor") != std::string::npos);
    }
    
    SECTION("Hair-specific fields conditionally included") {
        REQUIRE(content.find("#elif defined(HAIR) && defined(CS_HAIR)") != std::string::npos);
        REQUIRE(content.find("float hairShadow") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: IndirectContext Structure", "[Shaders][LightingCommon][Structs]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("IndirectContext struct defined") {
        REQUIRE(content.find("struct IndirectContext") != std::string::npos);
    }
    
    SECTION("Contains minimal required fields") {
        REQUIRE(content.find("float3 worldNormal") != std::string::npos);
        REQUIRE(content.find("float3 vertexNormal") != std::string::npos);
        REQUIRE(content.find("float3 viewDir") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: DirectLightingOutput Structure", "[Shaders][LightingCommon][Structs]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("DirectLightingOutput struct defined") {
        REQUIRE(content.find("struct DirectLightingOutput") != std::string::npos);
    }
    
    SECTION("Contains lighting output channels") {
        REQUIRE(content.find("float3 diffuse") != std::string::npos);
        REQUIRE(content.find("float3 specular") != std::string::npos);
        REQUIRE(content.find("float3 transmission") != std::string::npos);
    }
    
    SECTION("PBR coat diffuse conditionally included") {
        REQUIRE(content.find("float3 coatDiffuse") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: IndirectLobeWeights Structure", "[Shaders][LightingCommon][Structs]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("IndirectLobeWeights struct defined") {
        REQUIRE(content.find("struct IndirectLobeWeights") != std::string::npos);
    }
    
    SECTION("Contains lobe weights") {
        REQUIRE(content.find("float3 diffuse") != std::string::npos);
        REQUIRE(content.find("float3 specular") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: MaterialProperties Structure", "[Shaders][LightingCommon][Material]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("MaterialProperties struct defined") {
        REQUIRE(content.find("struct MaterialProperties") != std::string::npos);
    }
    
    SECTION("Always has BaseColor") {
        REQUIRE(content.find("float3 BaseColor") != std::string::npos);
    }
    
    SECTION("Conditional properties based on rendering path") {
        // Check for both PBR and non-PBR fields
        REQUIRE(content.find("float Roughness") != std::string::npos);
        REQUIRE(content.find("float3 F0") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: Utility Functions", "[Shaders][LightingCommon][Functions]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("ShininessToRoughness function defined") {
        REQUIRE(content.find("float ShininessToRoughness(float shininess)") != std::string::npos);
    }
    
    SECTION("ShininessToRoughness has correct formula") {
        REQUIRE(content.find("pow(abs(2.0 / (shininess + 2.0)), 0.25)") != std::string::npos);
    }
    
    SECTION("ReconstructTBN function defined") {
        REQUIRE(content.find("float3x3 ReconstructTBN") != std::string::npos);
    }
    
    SECTION("ReconstructTBN uses screen-space derivatives") {
        REQUIRE(content.find("ddx(worldPos)") != std::string::npos);
        REQUIRE(content.find("ddy(worldPos)") != std::string::npos);
        REQUIRE(content.find("ddx(uv)") != std::string::npos);
        REQUIRE(content.find("ddy(uv)") != std::string::npos);
    }
}

TEST_CASE("LightingCommon: Line Count", "[Shaders][LightingCommon][Stats]") {
    auto content = readFile("package/Shaders/Common/LightingCommon.hlsli");
    
    SECTION("File is approximately 112 lines") {
        int lineCount = std::count(content.begin(), content.end(), '\n');
        // Allow some tolerance for different line ending styles
        REQUIRE(lineCount >= 110);
        REQUIRE(lineCount <= 115);
    }
}

} // namespace Tests
} // namespace Shaders