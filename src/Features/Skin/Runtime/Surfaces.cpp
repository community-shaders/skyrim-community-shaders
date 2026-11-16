#include "Surfaces.h"

namespace SkinSurfaces
{
	namespace
	{
		bool Within(RE::NiAVObject* a_object, const RE::NiAVObject* a_root)
		{
			if (!a_root)
				return false;
			for (auto* current = a_object; current; current = current->parent)
				if (current == a_root)
					return true;
			return false;
		}
		// Native slot 2 in mesh order. A TXST record stores the same input in its fourth entry.
		std::string SkinTexturePath(RE::BSTextureSet* a_set)
		{
			if (!a_set)
				return {};
			if (auto* form = skyrim_cast<RE::BGSTextureSet*>(a_set)) {
				const auto* name = form->textures[3].textureName.c_str();
				return name ? name : "";
			}
			const auto* path = a_set->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(2));
			return path ? path : "";
		}
	}

	std::array<const char*, 5> PartNames()
	{
		return { T("feature.skin.head", "Head"), T("feature.skin.body", "Body"), T("feature.skin.hands", "Hands"),
			T("feature.skin.feet", "Feet"), T("feature.skin.other_part", "Other") };
	}

	std::string Key(RE::BSShaderProperty* a_property)
	{
		if (!SkinMaterials::IsCompatible(a_property))
			return {};
		auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(a_property->material);
		// The bound texture is what is rendered; the texture set is only what was last requested.
		if (material->diffuseTexture) {
			const auto* name = material->diffuseTexture->name.c_str();
			if (auto key = SkinMaterials::TextureKey(name ? name : ""); !key.empty())
				return key;
		}
		if (material->textureSet) {
			const auto* path = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
			return SkinMaterials::TextureKey(path ? path : "");
		}
		return {};
	}

	bool HasThickness(RE::BSShaderProperty* a_property)
	{
		if (!SkinMaterials::IsCompatible(a_property))
			return false;
		auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(a_property->material);
		return !SkinTexturePath(material->textureSet.get()).empty();
	}

	bool IsRaceMenuOverlay(const RE::BSGeometry* a_geometry, const RE::BSShaderProperty* a_property)
	{
		if (!a_geometry || !a_property || !a_property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned))
			return false;
		const auto* material = a_property->material;
		using Feature = RE::BSShaderMaterial::Feature;
		if (!material || material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
			(material->GetFeature() != Feature::kFaceGen && material->GetFeature() != Feature::kFaceGenRGBTint))
			return false;
		const auto* alpha = a_geometry->GetGeometryRuntimeData().alphaProperty.get();
		if (!alpha || !alpha->GetAlphaBlending())
			return false;
		const auto* geometryName = a_geometry->name.c_str();
		if (!geometryName)
			return false;
		std::string_view name = geometryName;
		static constexpr std::array<std::string_view, 5> parts{ "Body", "Hands", "Feet", "Face", "Hair" };
		const auto separator = name.find(" [");
		if (separator == std::string_view::npos || std::ranges::find(parts, name.substr(0, separator)) == parts.end())
			return false;
		name.remove_prefix(separator + 2);
		if (name.starts_with("SOvl"))
			name.remove_prefix(4);
		else if (name.starts_with("Ovl"))
			name.remove_prefix(3);
		else
			return false;
		if (!name.ends_with("]"))
			return false;
		name.remove_suffix(1);
		return !name.empty() && name.find_first_not_of("0123456789") == std::string_view::npos;
	}

	bool IsHead(RE::BSGeometry* a_geometry)
	{
		for (auto* parent = a_geometry ? a_geometry->parent : nullptr; parent; parent = parent->parent)
			if (netimmerse_cast<RE::BSFaceGenNiNode*>(parent))
				return true;
		return false;
	}

	bool Belongs(RE::BSGeometry* a_geometry, RE::Actor* a_actor)
	{
		return a_geometry && a_actor && (Within(a_geometry, a_actor->Get3D(false)) || (a_actor == RE::PlayerCharacter::GetSingleton() && Within(a_geometry, a_actor->Get3D(true))));
	}

	RE::Actor* Owner(RE::BSGeometry* a_geometry)
	{
		for (auto* object = static_cast<RE::NiAVObject*>(a_geometry); object; object = object->parent)
			if (auto* owner = object->GetUserData())
				if (auto* actor = owner->As<RE::Actor>(); Belongs(a_geometry, actor))
					return actor;
		auto* player = RE::PlayerCharacter::GetSingleton();
		return Belongs(a_geometry, player) ? player : nullptr;
	}

	Description Describe(RE::BSGeometry* a_geometry, RE::Actor* a_actor)
	{
		Description result;
		if (!Belongs(a_geometry, a_actor))
			return result;
		auto* third = a_actor->Get3D(false);
		auto* first = a_actor == RE::PlayerCharacter::GetSingleton() ? a_actor->Get3D(true) : nullptr;
		result.firstPerson = first && first != third && Within(a_geometry, first);
		if (IsHead(a_geometry)) {
			result.part = Part::Head;
			return result;
		}
		const auto& biped = a_actor->GetBiped(result.firstPerson);
		if (!biped)
			return result;
		for (uint32_t slot = 0; slot < RE::BIPED_OBJECTS::kTotal; ++slot) {
			const auto& object = biped->objects[slot];
			if (!object.partClone || !Within(a_geometry, object.partClone.get()))
				continue;
			result.part = slot == RE::BIPED_OBJECTS::kBody ? Part::Body : slot == RE::BIPED_OBJECTS::kHands ? Part::Hands :
			                                                          slot == RE::BIPED_OBJECTS::kFeet      ? Part::Feet :
			                                                                                                  Part::Other;
			if (object.item) {
				const auto* name = object.item->GetName();
				result.worn = name ? name : "";
			}
			break;
		}
		return result;
	}

	std::string Label(const Description& a_description)
	{
		std::string label = PartNames()[static_cast<size_t>(a_description.part)];
		if (!a_description.worn.empty())
			label += " - " + a_description.worn;
		if (a_description.firstPerson)
			label += T("feature.skin.suffix_first_person", " (first person)");
		return label;
	}
}
