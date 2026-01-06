#pragma once

#include "PCH.h"
#include "TruePBR.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "Features/Raytracing/Utils.h"
#include <filesystem>

namespace TextureSharing
{
	enum Type : uint32_t
	{
		None,
		Diffuse,
		Normal,
		Parallax,
		Specular,
		Skin,
		Glow,
		EnvMap,
		EnvMask,
		ModelSpaceNormal,
		RMAOS,
		Unkown
	};

	constexpr unsigned int FLAG_MASK = 0x80000000;

	static inline Type GetTextureType(const char* path)
	{
		if (!path)
			return Type::None;

		auto pathLower = ToLower(path);

		std::filesystem::path fsPath(pathLower);

		std::string filename = fsPath.stem().string();

		size_t pos = filename.rfind('_');

		if (pos != std::string::npos) {
			std::string suffix = filename.substr(pos + 1);

			if (suffix == "d")
				return Type::Diffuse;

			if (suffix == "n")
				return Type::Normal;

			if (suffix == "p")
				return Type::Parallax;

			if (suffix == "s")
				return Type::Specular;

			if (suffix == "g")
				return Type::Glow;

			if (suffix == "sk")
				return Type::Skin;

			if (suffix == "e")
				return Type::EnvMap;

			if (suffix == "m" || suffix == "em")
				return Type::EnvMask;

			if (suffix == "msn")
				return Type::ModelSpaceNormal;

			if (suffix == "rmaos")
				return Type::RMAOS;
		}

		// Yes, everything else falls back as diffuse
		return Type::Diffuse;
	}

	static bool IsNumber(const std::string& s)
	{
		return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
	}

	static inline Type GetTextureTypeSafe(const char* path)
	{
		if (!path)
			return Type::None;

		auto pathLower = ToLower(path);

		std::filesystem::path fsPath(pathLower);

		std::string filename = fsPath.stem().string();

		size_t pos = filename.rfind('_');

		if (pos != std::string::npos) {
			std::string suffix = filename.substr(pos + 1);

			if (suffix == "d" || IsNumber(suffix))
				return Type::Diffuse;

			if (suffix == "n")
				return Type::Normal;

			if (suffix == "p")
				return Type::Parallax;

			if (suffix == "s")
				return Type::Specular;

			if (suffix == "g")
				return Type::Glow;

			if (suffix == "sk")
				return Type::Skin;

			if (suffix == "e")
				return Type::EnvMap;

			if (suffix == "m" || suffix == "em")
				return Type::EnvMask;

			if (suffix == "msn")
				return Type::ModelSpaceNormal;

			if (suffix == "rmaos")
				return Type::RMAOS;

			logger::warn("[RT] GetTextureType - Unknown Suffix \"{}\"", suffix);
			return Type::Unkown;
		}

		return Type::Diffuse;
	}

	static inline bool ShouldShareTexture(const char* path, [[maybe_unused]] bool pathTracing)
	{
		auto type = GetTextureType(path);

		switch (type) {
		case TextureSharing::Diffuse:
		case TextureSharing::Normal:
		case TextureSharing::Specular:
		case TextureSharing::Glow:
		case TextureSharing::EnvMask:
		case TextureSharing::ModelSpaceNormal:
		case TextureSharing::RMAOS:
			return true;
		default:
			break;
		}

		return false;
	}
}