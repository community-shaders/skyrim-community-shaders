#pragma once

/** @brief PBR roles in the standard nine-entry NIF texture set. */
enum class PBRTextureSlot : uint32_t
{
	BaseColor = 0,
	Normal = 1,
	Emissive = 2,
	Displacement = 3,
	Reserved4 = 4,
	Rmaos = 5,
	Features1 = 6,
	Features0 = 7,
	Reserved8 = 8,
};

/** @brief Converts a PBR file slot to the engine texture-set index. */
constexpr RE::BSTextureSet::Texture PBRTextureIndex(PBRTextureSlot slot)
{
	return static_cast<RE::BSTextureSet::Texture>(slot);
}
