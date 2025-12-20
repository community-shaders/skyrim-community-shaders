#ifndef MATERIAL_HLSL
#define MATERIAL_HLSL

#ifndef __cplusplus
namespace ShaderType
{
    static const uint16_t Grass = 0;
    static const uint16_t Sky = 1;
    static const uint16_t Water = 2;
    static const uint16_t BloodSplatter = 3;
    static const uint16_t Lighting = 4;
    static const uint16_t Effect = 5;
    static const uint16_t DistantTree = 6;
    static const uint16_t Particle = 7; 
}

namespace Feature
{
	static const uint16_t kDefault = 0;
	static const uint16_t kEnvironmentMap = 1;
	static const uint16_t kGlowMap = 2;
	static const uint16_t kParallax = 3;
	static const uint16_t kFaceGen = 4;
	static const uint16_t kFaceGenRGBTint = 5;
	static const uint16_t kHairTint = 6;
	static const uint16_t kParallaxOcc = 7;
	static const uint16_t kMultiTexLand = 8;
	static const uint16_t kLODLand = 9;
	static const uint16_t kUnknown = 10;
	static const uint16_t kMultilayerParallax = 11;
	static const uint16_t kTreeAnim = 12;
	static const uint16_t kMultiIndexTriShapeSnow = 14;
	static const uint16_t kLODObjectsHD = 15;
	static const uint16_t kEye = 16;
	static const uint16_t kCloud = 17;
	static const uint16_t kLODLandNoise = 18;
	static const uint16_t kMultiTexLandLODBlend = 19;   
}
#endif

#ifdef __cplusplus
struct MaterialData
#else
struct Material
#endif
{
	half4 BaseColor;
	half4 EffectColor;
	half4 TexCoordOffsetScale;
	half roughness;
	//half specular;
	uint16_t BaseTexture;
	uint16_t NormalTexture;
	uint16_t EffectTexture;	
	uint16_t RMAOSTexture;
    uint16_t ShaderType: 3; // 8 types
    uint16_t Feature: 5;    // Max 32 features
    uint16_t PBRFlags: 8;   // Max 16 flags

#ifndef __cplusplus	
	float2 TexCoord(float2 texCoord)
    {
		return texCoord * TexCoordOffsetScale.zw + TexCoordOffsetScale.xy;
	}
#endif
};

#ifdef __cplusplus
static_assert(sizeof(MaterialData) % 4 == 0);
#endif

#endif