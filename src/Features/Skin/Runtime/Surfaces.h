#pragma once

#include "../Material/Material.h"

// What a loaded skin shape is: which texture it shows and whose body it belongs to.
namespace SkinSurfaces
{
	enum class Part : uint32_t
	{
		Head,
		Body,
		Hands,
		Feet,
		Other,
		Count
	};
	std::array<const char*, 5> PartNames();

	/// The material key of a shape: the diffuse texture that is actually bound, after every engine or
	/// plugin replacement (ARMA skin textures, race swaps in shared armor meshes, FaceGen, NiOverride).
	/// Empty when the texture cannot be identified as a DDS file.
	std::string Key(RE::BSShaderProperty* a_property);
	/// Whether the shape has a native skin (subsurface) texture, which drives transmission thickness.
	bool HasThickness(RE::BSShaderProperty* a_property);
	bool IsHead(RE::BSGeometry* a_geometry);
	bool Belongs(RE::BSGeometry* a_geometry, RE::Actor* a_actor);
	RE::Actor* Owner(RE::BSGeometry* a_geometry);

	/// Display-only classification. It never decides which material applies.
	struct Description
	{
		Part part = Part::Other;
		bool firstPerson = false;
		std::string worn;  ///< Name of the equipped item whose model contains the shape, when it has one.
	};
	Description Describe(RE::BSGeometry* a_geometry, RE::Actor* a_actor);
	std::string Label(const Description& a_description);
}
