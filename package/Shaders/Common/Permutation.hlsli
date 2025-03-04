#ifndef __PERMUTATION_DEPENDENCY_HLSL__
#define __PERMUTATION_DEPENDENCY_HLSL__

namespace Permutation
{

	namespace LightingTechnique
	{
		static const uint Glowmap = 2;
	}

	namespace LightingFlags
	{
		static const uint VertexColor = (1 << 0);
		static const uint Skinned = (1 << 1);
		static const uint ModelSpaceNormals = (1 << 2);
		// Flags 3 to 8 are unused
		static const uint Specular = (1 << 9);
		static const uint SoftLighting = (1 << 10);
		static const uint RimLighting = (1 << 11);
		static const uint BackLighting = (1 << 12);
		static const uint ShadowDir = (1 << 13);
		static const uint DefShadow = (1 << 14);
		static const uint ProjectedUV = (1 << 15);
		static const uint DepthWriteDecals = (1 << 15);  // (HAIR technique only)
		static const uint AnisoLighting = (1 << 16);
		static const uint AmbientSpecular = (1 << 17);
		static const uint WorldMap = (1 << 18);
		static const uint BaseObjectIsSnow = (1 << 19);
		static const uint DoAlphaTest = (1 << 20);
		static const uint Snow = (1 << 21);
		static const uint CharacterLight = (1 << 22);
		static const uint AdditionalAlphaMask = (1 << 23);
	}

	namespace WaterFlags
	{
		static const uint NormalTexCoord = (1 << 1);
		static const uint Reflections = (1 << 2);
		static const uint Refractions = (1 << 3);
		static const uint Depth = (1 << 4);
		static const uint Interior = (1 << 5);
		static const uint Wading = (1 << 6);
		static const uint VertexAlphaDepth = (1 << 7);
		static const uint Cubemap = (1 << 8);
		static const uint Flowmap = (1 << 9);
		static const uint BlendNormals = (1 << 10);
	}

	namespace EffectFlags
	{
		static const uint GrayscaleToColor = (1 << 19);
		static const uint GrayscaleToAlpha = (1 << 20);
		static const uint IgnoreTexAlpha = (1 << 21);
	}

	namespace ExtraFlags
	{
		static const uint InWorld = (1 << 0);
		static const uint InReflection = (1 << 1);
		static const uint IsBeastRace = (1 << 2);
		static const uint EffectShadows = (1 << 3);
		static const uint IsDecal = (1 << 4);
		static const uint IsTree = (1 << 5);
	}

	cbuffer PerShader : register(b4)
	{
		uint VertexShaderDescriptor;
		uint PixelShaderDescriptor;
		uint ExtraShaderDescriptor;
	};

}
#endif  // __PERMUTATION_DEPENDENCY_HLSL__