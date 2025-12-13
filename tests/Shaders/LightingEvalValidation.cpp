#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <string>
#include <algorithm>

namespace Shaders {
namespace Tests {

std::string readFileContent(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) return "";
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

TEST_CASE("LightingEval: File Existence", "[Shaders][LightingEval]") {
    SECTION("LightingEval.hlsli exists at correct path") {
        std::ifstream file("package/Shaders/Common/LightingEval.hlsli");
        REQUIRE(file.good());
    }
}

TEST_CASE("LightingEval: Include Structure", "[Shaders][LightingEval][Includes]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("Has proper include guard") {
        REQUIRE(content.find("#ifndef LIGHTING_EVAL_HLSLI") != std::string::npos);
        REQUIRE(content.find("#define LIGHTING_EVAL_HLSLI") != std::string::npos);
    }
    
    SECTION("Includes LightingCommon.hlsli") {
        REQUIRE(content.find("#include \"Common/LightingCommon.hlsli\"") != std::string::npos);
    }
    
    SECTION("Includes BRDF.hlsli") {
        REQUIRE(content.find("#include \"Common/BRDF.hlsli\"") != std::string::npos);
    }
    
    SECTION("Includes Math.hlsli") {
        REQUIRE(content.find("#include \"Common/Math.hlsli\"") != std::string::npos);
    }
    
    SECTION("Conditionally includes PBR.hlsli") {
        REQUIRE(content.find("#if defined(TRUE_PBR)") != std::string::npos);
        REQUIRE(content.find("#include \"Common/PBR.hlsli\"") != std::string::npos);
    }
}

TEST_CASE("LightingEval: CreateDirectLightingContext Function", "[Shaders][LightingEval][Functions]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("Function is defined") {
        REQUIRE(content.find("DirectContext CreateDirectLightingContext") != std::string::npos);
    }
    
    SECTION("Normalizes input vectors") {
        REQUIRE(content.find("normalize(worldNormal)") != std::string::npos);
        REQUIRE(content.find("normalize(vertexNormal)") != std::string::npos);
        REQUIRE(content.find("normalize(viewDir)") != std::string::npos);
        REQUIRE(content.find("normalize(lightDir)") != std::string::npos);
    }
    
    SECTION("Computes half vector") {
        REQUIRE(content.find("normalize(context.viewDir + context.lightDir)") != std::string::npos);
    }
    
    SECTION("Applies shadow factors") {
        REQUIRE(content.find("lightColor * shadowFactor * parallaxShadow") != std::string::npos);
    }
}

TEST_CASE("LightingEval: CreateIndirectLightingContext Function", "[Shaders][LightingEval][Functions]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("Function is defined") {
        REQUIRE(content.find("IndirectContext CreateIndirectLightingContext") != std::string::npos);
    }
    
    SECTION("Creates minimal context") {
        REQUIRE(content.find("IndirectContext context = (IndirectContext)0") != std::string::npos);
    }
}

TEST_CASE("LightingEval: VanillaSpecular Function", "[Shaders][LightingEval][Specular]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("Function is defined") {
        REQUIRE(content.find("float3 VanillaSpecular(DirectContext context") != std::string::npos);
    }
    
    SECTION("Computes HdotN") {
        REQUIRE(content.find("HdotN") != std::string::npos);
    }
    
    SECTION("Handles anisotropic lighting") {
        REQUIRE(content.find("#if defined(ANISO_LIGHTING)") != std::string::npos);
    }
    
    SECTION("Handles sparkle effect") {
        REQUIRE(content.find("#if defined(SPARKLE)") != std::string::npos);
    }
    
    SECTION("Uses Blinn-Phong specular") {
        REQUIRE(content.find("exp2(shininess * log2(HdotN))") != std::string::npos);
    }
}

TEST_CASE("LightingEval: EvaluateLighting Function", "[Shaders][LightingEval][Main]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("Function signature correct") {
        REQUIRE(content.find("void EvaluateLighting(DirectContext context, MaterialProperties material") != std::string::npos);
    }
    
    SECTION("Initializes output to zero") {
        REQUIRE(content.find("lightingOutput = (DirectLightingOutput)0") != std::string::npos);
    }
    
    SECTION("Routes to PBR for TRUE_PBR") {
        REQUIRE(content.find("PBR::GetDirectLightInput(lightingOutput, context, material") != std::string::npos);
    }
    
    SECTION("Routes to Hair for CS_HAIR") {
        REQUIRE(content.find("Hair::GetHairDirectLight(lightingOutput, context, material") != std::string::npos);
    }
    
    SECTION("Computes Lambert diffuse for standard path") {
        REQUIRE(content.find("saturate(NdotL) * context.lightColor") != std::string::npos);
    }
    
    SECTION("Handles soft lighting") {
        REQUIRE(content.find("#if defined(SOFT_LIGHTING)") != std::string::npos);
    }
    
    SECTION("Handles rim lighting") {
        REQUIRE(content.find("#if defined(RIM_LIGHTING)") != std::string::npos);
    }
    
    SECTION("Handles back lighting") {
        REQUIRE(content.find("#if defined(BACK_LIGHTING)") != std::string::npos);
    }
    
    SECTION("Uses VanillaSpecular") {
        REQUIRE(content.find("VanillaSpecular(context, material.Shininess, uv)") != std::string::npos);
    }
}

TEST_CASE("LightingEval: GetIndirectLobeWeights Function", "[Shaders][LightingEval][Indirect]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("Function defined") {
        REQUIRE(content.find("void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights") != std::string::npos);
    }
    
    SECTION("Routes to PBR implementation") {
        REQUIRE(content.find("PBR::GetIndirectLobeWeights(lobeWeights, context, material)") != std::string::npos);
    }
    
    SECTION("Routes to Hair implementation") {
        REQUIRE(content.find("Hair::GetHairIndirectLobeWeights(lobeWeights, context, material") != std::string::npos);
    }
    
    SECTION("Sets diffuse to BaseColor for standard path") {
        REQUIRE(content.find("lobeWeights.diffuse = material.BaseColor") != std::string::npos);
    }
    
    SECTION("Uses EnvBRDF for specular weight") {
        REQUIRE(content.find("BRDF::EnvBRDF(material.Roughness, NdotV)") != std::string::npos);
    }
    
    SECTION("Applies horizon occlusion") {
        REQUIRE(content.find("horizon = horizon * horizon") != std::string::npos);
    }
}

TEST_CASE("LightingEval: Wetness Effects", "[Shaders][LightingEval][Wetness]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("EvaluateWetnessLighting function defined") {
        REQUIRE(content.find("void EvaluateWetnessLighting") != std::string::npos);
    }
    
    SECTION("Wetness strength based on roughness") {
        REQUIRE(content.find("saturate(1 - roughness)") != std::string::npos);
    }
    
    SECTION("Uses water F0") {
        REQUIRE(content.find("const float wetnessF0 = 0.02") != std::string::npos);
    }
    
    SECTION("Uses GGX BRDF") {
        REQUIRE(content.find("BRDF::D_GGX") != std::string::npos);
        REQUIRE(content.find("BRDF::Vis_SmithJointApprox") != std::string::npos);
        REQUIRE(content.find("BRDF::F_Schlick") != std::string::npos);
    }
    
    SECTION("GetWetnessIndirectLobeWeights defined") {
        REQUIRE(content.find("float3 GetWetnessIndirectLobeWeights") != std::string::npos);
    }
}

TEST_CASE("LightingEval: Line Count", "[Shaders][LightingEval][Stats]") {
    auto content = readFileContent("package/Shaders/Common/LightingEval.hlsli");
    
    SECTION("File is approximately 230 lines") {
        int lineCount = std::count(content.begin(), content.end(), '\n');
        REQUIRE(lineCount >= 225);
        REQUIRE(lineCount <= 235);
    }
}

} // namespace Tests
} // namespace Shaders