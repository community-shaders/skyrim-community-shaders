#ifndef MATERIAL_HLSI
#define MATERIAL_HLSI

struct Material
{
	half4 BaseColor;
	half4 EffectColor;
	half4 TexCoordOffsetScale;
	uint16_t BaseTexture;
	uint16_t EffectTexture;
	uint16_t RmaosTexture;
	uint16_t ShaderType;
};

#ifdef __cplusplus
static_assert(sizeof(Material) % 4 == 0);
#endif

#endif