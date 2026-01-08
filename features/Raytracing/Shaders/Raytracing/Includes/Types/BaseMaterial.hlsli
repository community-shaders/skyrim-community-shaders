#ifndef BASEMATERIAL_HLSL
#define BASEMATERIAL_HLSL

#ifndef __cplusplus
namespace ShaderType
{
    static const uint16_t TruePBR = 0;
    static const uint16_t Lighting = 1;
    static const uint16_t Effect = 2;
    static const uint16_t Grass = 3;
    static const uint16_t Water = 4;
    static const uint16_t BloodSplatter = 5;
    static const uint16_t DistantTree = 6;
    static const uint16_t Particle = 7;
}

namespace ShaderFlags
{
	static const uint32_t kSpecular = (1 << 0);
    static const uint32_t kTempRefraction = (1 << 1);
    static const uint32_t kVertexAlpha = (1 << 2);
    static const uint32_t kGrayscaleToPaletteColor = (1 << 3);
    static const uint32_t kGrayscaleToPaletteAlpha = (1 << 4);
    static const uint32_t kFalloff = (1 << 5);
    static const uint32_t kRefraction = (1 << 6);
    static const uint32_t kProjectedUV = (1 << 7);
    static const uint32_t kVertexColors = (1 << 8);
    static const uint32_t kMultiTextureLandscape = (1 << 9);	
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
struct BaseMaterial
#endif
{
    uint32_t ShaderFlags;		// Max 32 flags	
    uint16_t ShaderType;
    uint16_t Feature;
    uint16_t PBRFlags;
	
	half4 Color0;
	half4 Color1;	

	half Scalar0;
	half Scalar1;
	half Scalar2;
	
	half4 TexCoordOffsetScale0;
	half4 TexCoordOffsetScale1;	
	
	// Textures
	uint16_t Texture0;
	uint16_t Texture1;
	uint16_t Texture2;
	uint16_t Texture3;
	uint16_t Texture4;
	uint16_t Texture5;
	uint16_t Texture6;
	uint16_t Texture7;
	uint16_t Texture8;
	uint16_t Texture9;	
	uint16_t Texture10;
	uint16_t Texture11;
	
#ifndef __cplusplus
	float2 TexCoord(float2 texCoord)
    {
		return texCoord * TexCoordOffsetScale0.zw + TexCoordOffsetScale0.xy;
	}
#endif
};

#ifdef __cplusplus
static_assert(sizeof(MaterialData) % 4 == 0);
#endif

#endif // BASEMATERIAL_HLSL