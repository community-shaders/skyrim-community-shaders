#include "Material.h"

#include <cctype>
#include <cmath>

namespace SkinMaterials
{
	std::array<const char*, TextureCount> TextureLabels()
	{
		return { T("feature.skin.skin_controls", "Skin controls"),
			T("feature.skin.wet_surface", "Wet surface"),
			T("feature.skin.detail_normal_mask", "Detail normal + mask") };
	}

	const char* ParameterLabel(size_t a_index)
	{
		switch (static_cast<Parameter>(a_index)) {
		case Parameter::Roughness:
			return T("feature.skin.primary_roughness", "Primary Roughness");
		case Parameter::Reflectance:
			return T("feature.skin.reflectance", "Reflectance (F0)");
		case Parameter::Fuzz:
			return T("feature.skin.fuzz", "Fuzz");
		case Parameter::DetailEnabled:
			return T("feature.skin.detail_enabled", "Enable detail");
		case Parameter::DetailStrength:
			return T("feature.skin.detail_strength", "Detail strength");
		case Parameter::DetailTiling:
			return T("feature.skin.detail_tiling", "Detail tiling");
		case Parameter::SSSAmount:
			return T("feature.skin.sss_amount", "SSS strength");
		case Parameter::Transmission:
			return T("feature.skin.transmission", "Transmission");
		case Parameter::TransmissionDepth:
			return T("feature.skin.transmission_depth", "Transmission depth");
		case Parameter::WetResponse:
			return T("feature.skin.wet_response", "Wet response");
		case Parameter::SecondaryRoughness:
			return T("feature.skin.secondary_roughness", "Secondary Roughness");
		case Parameter::SpecularTextureMultiplier:
			return T("feature.skin.specular_texture_multiplier", "Specular Texture Multiplier");
		case Parameter::SecondarySpecularStrength:
			return T("feature.skin.secondary_specular_strength", "Secondary Specular Strength");
		case Parameter::BaseColorMultiplier:
			return T("feature.skin.base_color_multiplier", "Base Color Multiplier");
		case Parameter::PhysicalMainRoughnessMultiplier:
			return T("feature.skin.physical_main_roughness_multiplier", "Physical Main Roughness Multiplier");
		case Parameter::PhysicalSecondRoughnessMultiplier:
			return T("feature.skin.physical_second_roughness_multiplier", "Physical Second Roughness Multiplier");
		case Parameter::PhysicalSpecularStrength:
			return T("feature.skin.physical_specular_multiplier", "Physical Specular Multiplier");
		case Parameter::ExtraEdgeRoughness:
			return T("feature.skin.extra_edge_roughness", "Extra Edge Roughness");
		case Parameter::FuzzRoughness:
			return T("feature.skin.fuzz_roughness", "Fuzz Roughness");
		case Parameter::FuzzF0:
			return T("feature.skin.fuzz_f0", "Fuzz F0");
		case Parameter::TransmissionEnabled:
			return T("feature.skin.transmission_enabled", "Enable transmission");
		case Parameter::BodyTilingMultiplier:
			return T("feature.skin.body_tiling_multiplier", "Body Tiling Multiplier");
		default:
			return "";
		}
	}
	const char* SettingName(size_t a_index)
	{
		static constexpr std::array<const char*, ParameterCount> names{
			"SkinMainRoughness", "F0", "FuzzStrength", "EnableSkinDetail", "SkinDetailStrength", "SkinDetailTiling", "SSSAmount", "Translucency", "sssWidth", "WetResponse", "SkinSecondRoughness", "SkinSpecularTexMultiplier", "SecondarySpecularStrength", "BaseColorMultiplier", "PhysicalMainRoughnessMultiplier", "PhysicalSecondRoughnessMultiplier", "PhysicalSpecularStrength", "ExtraEdgeRoughness", "FuzzRoughness", "FuzzF0", "UseSSS", "BodyTilingMultiplier"
		};
		return names.at(a_index);
	}

	std::span<const ParameterInfo, ParameterCount> ParameterTable()
	{
		static constexpr std::array<ParameterInfo, ParameterCount> fields{ {
#define SKIN_PARAMETER(id, name, label, value, min, max, integer) { name, label, value, min, max, integer },
#include "Parameters.def"
#undef SKIN_PARAMETER
		} };
		return fields;
	}

	namespace
	{
		std::string Canonical(std::string_view a_path)
		{
			std::string path(a_path);
			std::replace(path.begin(), path.end(), '\\', '/');
			std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (path.starts_with("data/"))
				path.erase(0, 5);
			return path;
		}
		size_t FindParameter(const std::string& a_name)
		{
			const auto fields = ParameterTable();
			for (size_t i = 0; i < fields.size(); ++i)
				if (a_name == fields[i].name)
					return i;
			return ParameterCount;
		}
		size_t FindTexture(const std::string& a_name)
		{
			for (size_t i = 0; i < TextureNames.size(); ++i)
				if (a_name == TextureNames[i])
					return i;
			return TextureCount;
		}
		// Two-state parameters are written as booleans; 0 and 1 are accepted for hand-written files.
		std::optional<float> ReadNumber(const json& a_value, const ParameterInfo& a_field)
		{
			if (a_field.integer) {
				if (a_value.is_boolean())
					return a_value.get<bool>() ? 1.0f : 0.0f;
				if (a_value.is_number() && (a_value.get<double>() == 0.0 || a_value.get<double>() == 1.0))
					return static_cast<float>(a_value.get<double>());
				return std::nullopt;
			}
			if (!a_value.is_number())
				return std::nullopt;
			const auto number = a_value.get<double>();
			return std::isfinite(number) ? std::optional(static_cast<float>(number)) : std::nullopt;
		}
		nlohmann::ordered_json WriteNumber(float a_value, const ParameterInfo& a_field)
		{
			if (a_field.integer)
				return a_value != 0;
			// Sliders work at three decimals; four keeps hand edits intact without float noise such as 0.699999988.
			return std::round(static_cast<double>(a_value) * 10000.0) / 10000.0;
		}
		std::string Range(const ParameterInfo& a_field)
		{
			return a_field.integer ? std::string("true / false") : std::format("{} - {}", a_field.minimum, a_field.maximum);
		}
		std::string Hex(uint64_t a_value)
		{
			return std::format("{:016x}", a_value);
		}
		uint64_t Unsigned(const json& a_value)
		{
			if (!a_value.is_number_integer() || (!a_value.is_number_unsigned() && a_value.get<int64_t>() < 0))
				throw std::runtime_error(T("feature.skin.material_file_invalid_fingerprint", "authoredFor must contain a size and a 16-digit hexadecimal hash."));
			return a_value.get<uint64_t>();
		}

		Definition Decode(const json& a_value)
		{
			if (!a_value.is_object() || !a_value.contains("format") || a_value.at("format") != Format)
				throw std::runtime_error(T("feature.skin.material_file_wrong_format", "Not a Skin material file: \"format\" must be \"CommunityShaders.SkinMaterial\"."));
			if (!a_value.contains("version") || a_value.at("version") != Version)
				throw std::runtime_error(T("feature.skin.material_file_wrong_version", "Unsupported Skin material file version."));
			Definition result;
			// A material file has no layer of its own to fall back to, so "explicitly none" and "not set" mean the same.
			result.material = Apply({}, DecodeChanges(a_value));
			if (a_value.contains("authoredFor")) {
				const auto& authored = a_value.at("authoredFor");
				const auto hash = authored.is_object() && authored.contains("hash") && authored.at("hash").is_string() ? authored.at("hash").get<std::string>() : std::string();
				if (hash.size() != 16 || !std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }) || !authored.contains("size"))
					throw std::runtime_error(T("feature.skin.material_file_invalid_fingerprint", "authoredFor must contain a size and a 16-digit hexadecimal hash."));
				result.authoredFor = Fingerprint{ Unsigned(authored.at("size")), std::stoull(hash, nullptr, 16) };
			}
			result.status = Status::Valid;
			return result;
		}
	}

	std::string NormalizeTexturePath(std::string a_path)
	{
		a_path = Canonical(a_path);
		if (a_path.empty())
			return {};
		if (!a_path.starts_with("textures/") || !a_path.ends_with(".dds") || a_path.size() > 1024 ||
			a_path.find(':') != std::string::npos || a_path.find("//") != std::string::npos ||
			a_path.find("/../") != std::string::npos || a_path.find("/./") != std::string::npos ||
			std::any_of(a_path.begin(), a_path.end(), [](unsigned char c) { return c < 32; }))
			throw std::runtime_error(T("feature.skin.choose_a_dds_resource_inside_textures", "Choose a DDS resource inside textures/."));
		for (size_t start = 0; start < a_path.size();) {
			const auto end = a_path.find('/', start);
			const auto segment = a_path.substr(start, end == std::string::npos ? end : end - start);
			const auto stem = segment.substr(0, segment.find('.'));
			if (segment.empty() || segment.back() == '.' || segment.back() == ' ' ||
				segment.find_first_of("<>\"|?*") != std::string::npos || stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
				(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'))
				throw std::runtime_error(T("feature.skin.choose_a_dds_resource_inside_textures", "Choose a DDS resource inside textures/."));
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		return a_path;
	}

	std::string TextureKey(std::string_view a_path)
	{
		auto path = Canonical(a_path);
		if (path.empty())
			return {};
		// Texture names are usually relative to Data or to Data/textures, but some loaders keep a longer prefix.
		if (!path.starts_with("textures/")) {
			const auto nested = path.rfind("/textures/");
			if (nested != std::string::npos)
				path.erase(0, nested + 1);
			else
				path.insert(0, "textures/");
		}
		try {
			return NormalizeTexturePath(std::move(path));
		} catch (const std::exception&) {
			return {};
		}
	}

	std::string SidecarPath(const std::string& a_key)
	{
		return a_key.size() > 4 ? a_key.substr(0, a_key.size() - 4) + SidecarExtension : std::string();
	}

	void Validate(const Material& a_material)
	{
		const auto fields = ParameterTable();
		for (size_t i = 0; i < fields.size(); ++i) {
			const float value = a_material.parameters.values[i];
			const auto& field = fields[i];
			if (!std::isfinite(value) || value < field.minimum || value > field.maximum ||
				(field.integer && value != 0.0f && value != 1.0f))
				throw std::runtime_error(std::string(T("feature.skin.invalid_value", "Invalid value: ")) + ParameterLabel(i));
		}
		for (const auto& path : a_material.textures)
			NormalizeTexturePath(path);
	}

	void Validate(const Changes& a_changes)
	{
		Material material;
		for (size_t i = 0; i < ParameterCount; ++i)
			if (a_changes.parameters[i])
				material.parameters.values[i] = *a_changes.parameters[i];
		for (size_t i = 0; i < a_changes.textures.size(); ++i) {
			const auto& texture = a_changes.textures[i];
			if (texture.mode > TextureMode::Default || (texture.mode == TextureMode::Resource && texture.path.empty()) ||
				(texture.mode != TextureMode::Resource && !texture.path.empty()))
				throw std::runtime_error(T("feature.skin.invalid_texture_selection", "Invalid texture selection."));
			material.textures[i] = texture.path;
		}
		Validate(material);
	}

	Material Apply(Material a_base, const Changes& a_changes)
	{
		for (size_t i = 0; i < ParameterCount; ++i)
			if (a_changes.parameters[i]) {
				a_base.parameters.values[i] = *a_changes.parameters[i];
				a_base.specified[i] = true;
			}
		for (size_t i = 0; i < a_changes.textures.size(); ++i) {
			const auto& texture = a_changes.textures[i];
			if (texture.mode != TextureMode::Inherit)
				a_base.textures[i] = texture.mode == TextureMode::Resource ? texture.path : "";
		}
		a_base.enabled = true;
		return a_base;
	}

	Material Resolve(Material a_material, const Parameters& a_defaults)
	{
		for (size_t i = 0; i < ParameterCount; ++i)
			if (!a_material.enabled || !a_material.specified[i])
				a_material.parameters.values[i] = a_defaults.values[i];
		if (!a_material.enabled)
			a_material.textures = {};
		a_material.specified.fill(true);
		a_material.enabled = true;
		return a_material;
	}

	Changes ScalarsOnly(Changes a_changes)
	{
		a_changes.textures = {};
		return a_changes;
	}

	bool IsCompatible(const RE::BSShaderProperty* a_property)
	{
		const auto* material = a_property ? a_property->material : nullptr;
		using Feature = RE::BSShaderMaterial::Feature;
		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		return material && material->GetType() == RE::BSShaderMaterial::Type::kLighting &&
		       (material->GetFeature() == Feature::kFaceGen || material->GetFeature() == Feature::kFaceGenRGBTint) &&
		       !a_property->flags.any(Flag::kMenuScreen, Flag::kEnvMap, Flag::kMultiLayerParallax, Flag::kDecal, Flag::kDynamicDecal);
	}

	nlohmann::ordered_json Encode(const Changes& a_changes)
	{
		auto parameters = nlohmann::ordered_json::object(), textures = nlohmann::ordered_json::object();
		const auto fields = ParameterTable();
		for (const auto parameter : ParameterOrder) {
			const auto i = static_cast<size_t>(parameter);
			if (a_changes.parameters[i])
				parameters[fields[i].name] = WriteNumber(*a_changes.parameters[i], fields[i]);
		}
		// An empty string means "explicitly none"; an absent entry inherits.
		for (size_t i = 0; i < TextureNames.size(); ++i)
			if (a_changes.textures[i].mode != TextureMode::Inherit)
				textures[TextureNames[i]] = a_changes.textures[i].path;
		return { { "parameters", parameters }, { "textures", textures } };
	}

	nlohmann::ordered_json Encode(const Material& a_material, const std::optional<Fingerprint>& a_authoredFor)
	{
		Changes changes;
		for (size_t i = 0; i < ParameterCount; ++i)
			if (a_material.specified[i])
				changes.parameters[i] = a_material.parameters.values[i];
		for (size_t i = 0; i < TextureCount; ++i)
			if (!a_material.textures[i].empty())
				changes.textures[i] = { TextureMode::Resource, a_material.textures[i] };
		const auto body = Encode(changes);
		nlohmann::ordered_json result{ { "format", Format }, { "version", Version } };
		result["parameters"] = body.at("parameters");
		result["textures"] = body.at("textures");
		if (a_authoredFor)
			result["authoredFor"] = { { "size", a_authoredFor->size }, { "hash", Hex(a_authoredFor->hash) } };
		return result;
	}

	Definition Parse(std::span<const uint8_t> a_bytes)
	{
		try {
			// Comments are allowed so authors can annotate hand-written files.
			return Decode(json::parse(a_bytes.begin(), a_bytes.end(), nullptr, true, true));
		} catch (const std::exception& e) {
			Definition result;
			result.status = Status::Invalid;
			result.diagnostic = e.what();
			return result;
		}
	}

	Changes DecodeChanges(const json& a_value)
	{
		if (!a_value.is_object())
			throw std::runtime_error(T("feature.skin.material_file_expected_object", "Expected an object with \"parameters\" and \"textures\"."));
		Changes result;
		const auto fields = ParameterTable();
		if (a_value.contains("parameters")) {
			const auto& parameters = a_value.at("parameters");
			if (!parameters.is_object())
				throw std::runtime_error(T("feature.skin.material_file_parameters_object", "\"parameters\" must be an object."));
			for (const auto& [name, value] : parameters.items()) {
				const auto index = FindParameter(name);
				if (index == ParameterCount)
					throw std::runtime_error(T("feature.skin.material_file_unknown_parameter", "Unknown parameter: ") + name);
				const auto number = ReadNumber(value, fields[index]);
				if (!number || *number < fields[index].minimum || *number > fields[index].maximum)
					throw std::runtime_error(I18n::GetSingleton()->Format("feature.skin.material_file_parameter_range", { { "name", name }, { "range", Range(fields[index]) } }, "{name} must be within {range}."));
				result.parameters[index] = *number;
			}
		}
		if (a_value.contains("textures")) {
			const auto& textures = a_value.at("textures");
			if (!textures.is_object())
				throw std::runtime_error(T("feature.skin.material_file_textures_object", "\"textures\" must be an object."));
			for (const auto& [name, value] : textures.items()) {
				const auto index = FindTexture(name);
				if (index == TextureCount)
					throw std::runtime_error(T("feature.skin.material_file_unknown_texture", "Unknown texture input (use controls, wet or detail): ") + name);
				if (!value.is_string())
					throw std::runtime_error(T("feature.skin.material_file_texture_string", "Texture inputs must be resource paths: ") + name);
				const auto path = NormalizeTexturePath(value.get<std::string>());
				result.textures[index] = { path.empty() ? TextureMode::Default : TextureMode::Resource, path };
			}
		}
		return result;
	}
}
