#pragma once

#include "Material.h"

// Material files found beside skin diffuse textures.
namespace SkinLibrary
{
	enum class Origin
	{
		None,     ///< No material file exists for this texture.
		Loose,    ///< Loose file in Data (possibly a mod manager's virtual Data).
		Archive,  ///< Packed in a BSA.
		Session   ///< Saved from the editor in this session; the file may not be visible to the game yet.
	};
	struct Entry
	{
		std::string key;   ///< Normalized diffuse texture path.
		std::string path;  ///< Resource path of the material file.
		SkinMaterials::Definition definition;
		Origin origin = Origin::None;
		std::string savedTo;  ///< File written by the editor, for `Origin::Session`.
	};
	using Pointer = std::shared_ptr<const Entry>;

	/// Reads the material file for a texture through the game's resource system. Never throws.
	Entry Read(const std::string& a_key);
}
