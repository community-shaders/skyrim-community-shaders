#pragma once

#include "I18n/I18n.h"
#include <array>
#include <optional>
#include <span>

// Advanced Skin material model.
//
// A material belongs to a skin *texture*, not to a mesh or an actor: it is looked up by the diffuse
// texture that is actually applied to a shape and stored next to that texture as
// `<diffuse name>.csskin.json`. See docs/advanced-skin-materials.md.
namespace SkinMaterials
{
	inline constexpr uint32_t Version = 1;
	inline constexpr const char* Format = "CommunityShaders.SkinMaterial";
	inline constexpr const char* SidecarExtension = ".csskin.json";

	enum class Parameter : uint32_t
	{
#define SKIN_PARAMETER(id, name, label, value, min, max, integer) id,
#include "Parameters.def"
#undef SKIN_PARAMETER
		Count
	};
	inline constexpr size_t ParameterCount = static_cast<size_t>(Parameter::Count);
	inline constexpr std::array<Parameter, ParameterCount> ParameterOrder{
		Parameter::Roughness, Parameter::SecondaryRoughness, Parameter::SpecularTextureMultiplier,
		Parameter::SecondarySpecularStrength, Parameter::Reflectance, Parameter::BaseColorMultiplier,
		Parameter::PhysicalMainRoughnessMultiplier, Parameter::PhysicalSecondRoughnessMultiplier, Parameter::PhysicalSpecularStrength,
		Parameter::ExtraEdgeRoughness, Parameter::Fuzz, Parameter::FuzzRoughness, Parameter::FuzzF0,
		Parameter::TransmissionEnabled, Parameter::Transmission, Parameter::TransmissionDepth, Parameter::SSSAmount,
		Parameter::DetailEnabled, Parameter::DetailStrength, Parameter::DetailTiling, Parameter::BodyTilingMultiplier, Parameter::WetResponse
	};

	/// Extension textures, in the order used everywhere else: Skin controls (RFAOS), wet surface, detail.
	inline constexpr size_t TextureCount = 3;
	inline constexpr std::array<const char*, TextureCount> TextureNames{ "controls", "wet", "detail" };
	std::array<const char*, TextureCount> TextureLabels();
	const char* ParameterLabel(size_t a_index);
	const char* SettingName(size_t a_index);

	struct ParameterInfo
	{
		const char* name;  ///< Key used in material files and character data.
		const char* label;
		float initial;
		float minimum;
		float maximum;
		bool integer;
	};
	std::span<const ParameterInfo, ParameterCount> ParameterTable();

	struct Parameters
	{
		std::array<float, ParameterCount> values{
#define SKIN_PARAMETER(id, name, label, value, min, max, integer) value,
#include "Parameters.def"
#undef SKIN_PARAMETER
		};
		float& operator[](Parameter a_parameter) { return values[static_cast<size_t>(a_parameter)]; }
		float operator[](Parameter a_parameter) const { return values[static_cast<size_t>(a_parameter)]; }
		bool operator==(const Parameters&) const = default;
	};

	/// A sparse material: only `specified` parameters and non-empty textures override the layer below.
	struct Material
	{
		Parameters parameters;
		std::array<bool, ParameterCount> specified{};
		std::array<std::string, TextureCount> textures;
		bool enabled = false;
		bool operator==(const Material&) const = default;
	};

	/// Identifies the diffuse texture a material was authored for. Advisory only.
	struct Fingerprint
	{
		uint64_t size = 0;
		uint64_t hash = 0;
		bool operator==(const Fingerprint&) const = default;
	};

	enum class Status
	{
		Missing,  ///< No material file: the common settings apply.
		Valid,
		Invalid  ///< The file exists but is ignored as a whole; see `diagnostic`.
	};
	struct Definition
	{
		Material material;
		Status status = Status::Missing;
		std::string diagnostic;
		std::optional<Fingerprint> authoredFor;
	};

	enum class TextureMode : uint32_t
	{
		Inherit,
		Resource,
		Default
	};
	struct TextureEdit
	{
		TextureMode mode = TextureMode::Inherit;
		std::string path;
		bool operator==(const TextureEdit&) const = default;
	};
	/// Sparse per-character changes layered over a resolved material.
	struct Changes
	{
		std::array<std::optional<float>, ParameterCount> parameters;
		std::array<TextureEdit, TextureCount> textures;
		bool operator==(const Changes&) const = default;
		bool Empty() const { return *this == Changes{}; }
	};

	bool IsCompatible(const RE::BSShaderProperty* a_property);

	/// Strict: a game resource path of an extension DDS. Throws when the path is not usable.
	std::string NormalizeTexturePath(std::string a_path);
	/// Lenient: normalizes an applied diffuse path into a material key. Returns an empty string when
	/// the name does not identify a DDS file (for example a generated render target).
	std::string TextureKey(std::string_view a_path);
	/// Resource path of the material file belonging to a material key.
	std::string SidecarPath(const std::string& a_key);

	void Validate(const Material& a_material);
	void Validate(const Changes& a_changes);
	Material Apply(Material a_base, const Changes& a_changes);
	Material Resolve(Material a_material, const Parameters& a_defaults);
	/// Drops texture edits; used for the whole-character layer, which cannot carry UV-bound data.
	Changes ScalarsOnly(Changes a_changes);

	// Files are written for people to read and edit, so members keep a meaningful order.
	nlohmann::ordered_json Encode(const Material& a_material, const std::optional<Fingerprint>& a_authoredFor);
	/// Never throws. A file that fails any check is reported as `Status::Invalid` and not applied.
	Definition Parse(std::span<const uint8_t> a_bytes);

	/// `{ "parameters": {...}, "textures": {...} }`. An empty texture path means "explicitly none".
	nlohmann::ordered_json Encode(const Changes& a_changes);
	/// Strict: throws on an unknown name, a value outside its range or an unusable texture path.
	Changes DecodeChanges(const json& a_value);
}
