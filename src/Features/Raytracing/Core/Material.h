#pragma once

#include "PCH.h"

#include <d3d12.h>
#include <winrt/base.h>

#include "Raytracing/Includes/Types/Material.hlsli"

using namespace magic_enum::bitwise_operators;

struct Material
{
	static constexpr uint MAX_LAND_TEXTURES = 5u;
	static constexpr uint MAX_PBRLAND_TEXTURES = 6u;

	enum ShaderType : uint16_t
	{
		TruePBR = 0,
		Lighting = 1,
		Effect = 2,
		Grass = 3,
		Water = 4,
		BloodSplatter = 5,
		DistantTree = 6,
		Particle = 7
	};

	// We have a limited number of bits and not all types are necessary
	ShaderType GetShaderType() const
	{
		if (shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen))
			return ShaderType::TruePBR;

		switch (shaderType) {
		case RE::BSShader::Type::Grass:
			return ShaderType::Grass;
		case RE::BSShader::Type::Water:
			return ShaderType::Water;
		case RE::BSShader::Type::BloodSplatter:
			return ShaderType::BloodSplatter;
		case RE::BSShader::Type::Effect:
			return ShaderType::Effect;
		case RE::BSShader::Type::DistantTree:
			return ShaderType::DistantTree;
		case RE::BSShader::Type::Particle:
			return ShaderType::Particle;
		default:
			return ShaderType::Lighting;
		}
	}

	enum ShaderFlags : uint32_t
	{
		None = 0,
		kSpecular = 1 << 0,
		kTempRefraction = 1 << 1,
		kVertexAlpha = 1 << 2,
		kGrayscaleToPaletteColor = 1 << 3,
		kGrayscaleToPaletteAlpha = 1 << 4,
		kFalloff = 1 << 5,
		kEnvMap = 1 << 6,
		kFace = 1 << 7,
		kModelSpaceNormals = 1 << 8,
		kRefraction = 1 << 9,
		kProjectedUV = 1 << 10,
		kExternalEmittance = 1 << 11,
		kVertexColors = 1 << 12,
		kMultiTextureLandscape = 1 << 13,
		kEyeReflect = 1 << 14,
		kHairTint = 1 << 15,
		kTwoSided = 1 << 16,
		kAssumeShadowmask = 1 << 17,
		kBackLighting = 1 << 18
	};

	enum AlphaFlags : uint16_t
	{
		kOpaque = 0,
		kAlphaBlend = 1 << 0,
		kAlphaTest = 1 << 1
	};

	ShaderFlags GetShaderFlags() const
	{
		using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

		auto shaderFlagsLocal = ShaderFlags::None;

		/*const auto& entries = magic_enum::enum_entries<ShaderFlags>();
		const auto& originalEntries = magic_enum::enum_entries<RE::BSShaderProperty::EShaderPropertyFlag>();

		for (const auto& [flag, name] : entries) {
			for (const auto& [originalFlag, originalName] : originalEntries) {
				if (shaderFlags.any(originalFlag) && name == originalName) {
					shaderFlagsLocal |= flag;
					break;
				}
			}
		}*/

		if (shaderFlags.any(EShaderPropertyFlag::kSpecular)) {
			shaderFlagsLocal |= ShaderFlags::kSpecular;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kTempRefraction)) {
			shaderFlagsLocal |= ShaderFlags::kTempRefraction;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kVertexAlpha)) {
			shaderFlagsLocal |= ShaderFlags::kVertexAlpha;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kGrayscaleToPaletteColor)) {
			shaderFlagsLocal |= ShaderFlags::kGrayscaleToPaletteColor;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kGrayscaleToPaletteAlpha)) {
			shaderFlagsLocal |= ShaderFlags::kGrayscaleToPaletteAlpha;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kFalloff)) {
			shaderFlagsLocal |= ShaderFlags::kFalloff;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kEnvMap)) {
			shaderFlagsLocal |= ShaderFlags::kEnvMap;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kFace)) {
			shaderFlagsLocal |= ShaderFlags::kFace;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kModelSpaceNormals)) {
			shaderFlagsLocal |= ShaderFlags::kModelSpaceNormals;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kRefraction)) {
			shaderFlagsLocal |= ShaderFlags::kRefraction;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kProjectedUV)) {
			shaderFlagsLocal |= ShaderFlags::kProjectedUV;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kExternalEmittance)) {
			shaderFlagsLocal |= ShaderFlags::kExternalEmittance;
		}
		
		if (shaderFlags.any(EShaderPropertyFlag::kVertexColors)) {
			shaderFlagsLocal |= ShaderFlags::kVertexColors;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kMultiTextureLandscape)) {
			shaderFlagsLocal |= ShaderFlags::kMultiTextureLandscape;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kEyeReflect)) {
			shaderFlagsLocal |= ShaderFlags::kEyeReflect;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kHairTint)) {
			shaderFlagsLocal |= ShaderFlags::kHairTint;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kTwoSided)) {
			shaderFlagsLocal |= ShaderFlags::kTwoSided;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kAssumeShadowmask)) {
			shaderFlagsLocal |= ShaderFlags::kAssumeShadowmask;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kBackLighting)) {
			shaderFlagsLocal |= ShaderFlags::kBackLighting;
		}

		return shaderFlagsLocal;
	}

	REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	RE::BSShader::Type shaderType;
	RE::BSShaderMaterial::Feature Feature;
	stl::enumeration<PBRShaderFlags, uint16_t> PBRFlags;

	uint16_t AlphaFlags;

	eastl::array<half4, 3> Colors;
	eastl::array<half, 3> Scalars;

	eastl::array<half4, 2> TexCoordOffsetScale;

	eastl::array<eastl::shared_ptr<Allocation>, 20> Textures;

	MaterialData GetData() const
	{
		return MaterialData(
			TexCoordOffsetScale[0], TexCoordOffsetScale[1],
			Colors[0], Colors[1], Colors[2],
			Scalars[0], Scalars[1], Scalars[2], 0,
			AlphaFlags,
			Textures[0]->GetIndex(),
			Textures[1]->GetIndex(),
			Textures[2]->GetIndex(),
			Textures[3]->GetIndex(),
			Textures[4]->GetIndex(),
			Textures[5]->GetIndex(),
			Textures[6]->GetIndex(),
			Textures[7]->GetIndex(),
			Textures[8]->GetIndex(),
			Textures[9]->GetIndex(),
			Textures[10]->GetIndex(),
			Textures[11]->GetIndex(),
			Textures[12]->GetIndex(),
			Textures[13]->GetIndex(),
			Textures[14]->GetIndex(),
			Textures[15]->GetIndex(),
			Textures[16]->GetIndex(),
			Textures[17]->GetIndex(),
			Textures[18]->GetIndex(),
			Textures[19]->GetIndex(),
			GetShaderType(),
			static_cast<uint16_t>(Feature),
			PBRFlags.underlying(),
			static_cast<uint32_t>(GetShaderFlags()));
	}
};