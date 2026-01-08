#ifndef MATERIAL_HLSL
#define MATERIAL_HLSL

#include "Raytracing/Includes/Types/BaseMaterial.hlsli"

struct VanillaMaterial : BaseMaterial
{
    half4 BaseColor()
    {
		return Color0;
	}
	
	// Color scale on Alpha
    half4 SpecularColor()
    {
		return Color1;
	}		
	
    half SpecularPower()
    {
		return Scalar0;
	}
	
	// Textures
    uint16_t DiffuseTexture()
    {
		return Texture0;
	}
	
    uint16_t NormalTexture()
    {
		return Texture1;
	}
	
    uint16_t SpecularTexture()
    {
		return Texture2;
	}	
	
    uint16_t GlowTexture()
    {
		return Texture3;
	}		
	
    uint16_t EnvMaskTexture()
    {
		return Texture4;
	}
};

// In theory we should inherit from VanillaMaterial...
struct VanillaLandMaterial : BaseMaterial
{
	// Scalars
    half2 TexOffset()
    {
		return half2(Scalar0, Scalar1);
	}
	
    half TexFade()
    {
		return Scalar2;
	}	
	
	half4 BlendParams()
    {
		return Color0;
	}
	
	// Diffuse
    uint16_t DiffuseTexture0()
    {
		return Texture0;
	}

    uint16_t DiffuseTexture1()
    {
		return Texture1;
	}
	
    uint16_t DiffuseTexture2()	
    {
		return Texture2;
	}

    uint16_t DiffuseTexture3()
    {
		return Texture3;
	}

    uint16_t DiffuseTexture4()
    {
		return Texture4;
	}	
	
	// Normal
    uint16_t NormalTexture0()
    {
		return Texture5;
	}

    uint16_t NormalTexture1()
    {
		return Texture6;
	}
	
    uint16_t NormalTexture2()	
    {
		return Texture7;
	}

    uint16_t NormalTexture3()
    {
		return Texture8;
	}

    uint16_t NormalTexture4()
    {
		return Texture9;
	}
	
	// Misc
    uint16_t OverlayTexture()
    {
		return Texture10;
	}
	
    uint16_t NoiseTexture()
    {
		return Texture11;
	}
};

struct PBRMaterial : BaseMaterial
{
    half4 BaseColor()
    {
		return Color0;
	}
	
    half4 EffectColor()
    {
		return Color1;
	}		
	
    half RoughnessScale()
    {
		return Scalar0;
	}
	
    half SpecularLevel()
    {
		return Scalar1;
	}	
	
    uint16_t AlbedoTexture()
    {
		return Texture0;
	}
	
    uint16_t NormalTexture()
    {
		return Texture1;
	}
	
    uint16_t EffectTexture()
    {
		return Texture2;
	}
	
    uint16_t RMAOSTexture()
    {
		return Texture3;
	}	
};
#endif