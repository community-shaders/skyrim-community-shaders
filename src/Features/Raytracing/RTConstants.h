#pragma once

#include "PCH.h"

namespace RTConstants
{
	// DX12 will not like if we don't respect these numbers and try to write over the resource end
	static constexpr uint MAX_TEXTURES = 1024;
	static constexpr uint MAX_MODELS = 1024;
	static constexpr uint MAX_SHAPES = MAX_MODELS * 6;
	static constexpr uint MAX_MATERIALS = MAX_SHAPES;
	static constexpr uint MAX_INSTANCES = 4096;
	static constexpr uint MAX_LIGHTS = 255;

	static constexpr uint SKY_CUBEMAP_SIZE = 256;
	static constexpr uint SKY_HEMI_SIZE = SKY_CUBEMAP_SIZE * 2;

	static constexpr uint PLAYER_REFR_FORMID = 0x00000014;

	static constexpr uint MATERIAL_NORMALMAP_ID = 1;
}