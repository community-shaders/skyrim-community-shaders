#ifndef MATERIAL_HLSI
#define MATERIAL_HLSI

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
	uint16_t ShaderType;
	
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