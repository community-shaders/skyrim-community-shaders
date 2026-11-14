#pragma once

#include "../Runtime/Textures.h"
#include "Files.h"

namespace SkinEditor
{
	struct TextureTools
	{
		std::array<std::string, 4> channels;
		std::array<float, 4> defaults{ 1, 1, 1, 1 };
		bool compress = true;
		bool detail = false;
		std::string normal;
		std::string mask;
	};
	Bytes PackControls(const TextureTools& a_options);
	Bytes PackDetail(const TextureTools& a_options);
	SkinTextures::Resource ChannelPreview(const std::string& a_path, int a_channel);
	void CheckTextures(const SkinMaterials::Material& a_material);
}
