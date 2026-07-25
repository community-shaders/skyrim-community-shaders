#include "Features/Effect11/ParticleLights.h"

#include <algorithm>
#include <cstring>
#include <exception>

namespace
{
	std::optional<std::string> GetLowercaseStem(const char* a_path, const char* a_extension)
	{
		std::filesystem::path p(a_path);
		auto stem = p.stem().string();
		if (stem.empty())
			return std::nullopt;
		std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		(void)a_extension;
		return stem;
	}

	std::optional<std::string> GetLowercaseStemFromPath(const std::string& a_path, const char* a_extension)
	{
		std::filesystem::path p(a_path);
		auto stem = p.stem().string();
		if (stem.empty())
			return std::nullopt;
		std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		(void)a_extension;
		return stem;
	}
}

void Effect11PL::ConfigStore::Load()
{
	++configVersion;
	configs.clear();
	gradientConfigs.clear();

	configs["default"] = Config{};
	logger::info("[Effect11 PL] Particle lights config conflict policy: first-win");

	if (std::filesystem::exists("Data\\ParticleLights")) {
		logger::info("[Effect11 PL] Loading particle lights configs");

		auto iniFiles = clib_util::distribution::get_configs("Data\\ParticleLights", "", ".ini");
		std::sort(iniFiles.begin(), iniFiles.end());

		if (iniFiles.empty()) {
			logger::warn("[Effect11 PL] No .ini files in Data\\ParticleLights");
			return;
		}

		logger::info("[Effect11 PL] {} matching inis found", iniFiles.size());

		for (auto& path : iniFiles) {
			logger::info("[Effect11 PL] loading ini: {}", path);

			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetMultiKey();

			if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
				logger::error("\t\t[Effect11 PL] couldn't read INI");
				continue;
			}

			Config data{};
			data.cull = ini.GetBoolValue("Light", "Cull", false);
			data.colorMult.red = (float)ini.GetDoubleValue("Light", "ColorMultRed", 1.0);
			data.colorMult.green = (float)ini.GetDoubleValue("Light", "ColorMultGreen", 1.0);
			data.colorMult.blue = (float)ini.GetDoubleValue("Light", "ColorMultBlue", 1.0);
			data.saturationMult = (float)ini.GetDoubleValue("Light", "SaturationMult", 1.0);

			const auto filename = GetLowercaseStemFromPath(path, ".ini");
			if (!filename)
				continue;

			if (configs.contains(*filename)) {
				logger::warn("[Effect11 PL] Duplicate config '{}'; keeping first, ignoring {}", *filename, path);
				continue;
			}

			logger::debug("[Effect11 PL] Inserting {}", *filename);
			configs.emplace(*filename, data);
		}
	}

	if (std::filesystem::exists("Data\\ParticleLights\\Gradients")) {
		logger::info("[Effect11 PL] Loading gradient configs");

		auto iniFiles = clib_util::distribution::get_configs("Data\\ParticleLights\\Gradients", "", ".ini");
		std::sort(iniFiles.begin(), iniFiles.end());

		if (iniFiles.empty()) {
			logger::warn("[Effect11 PL] No .ini files in Data\\ParticleLights\\Gradients");
			return;
		}

		logger::info("[Effect11 PL] {} matching inis found", iniFiles.size());

		for (auto& path : iniFiles) {
			logger::info("[Effect11 PL] loading ini: {}", path);

			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetMultiKey();

			if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
				logger::error("\t\t[Effect11 PL] couldn't read INI");
				continue;
			}

			GradientConfig data{};
			const char* value = ini.GetValue("Gradient", "Color");
			if (!value || strcmp(value, "") == 0) {
				logger::error("[Effect11 PL] missing color");
				continue;
			}

			std::string_view str = value;
			if (str.starts_with("0x"))
				str.remove_prefix(2);
			if (str.starts_with("#"))
				str.remove_prefix(1);

			constexpr std::string_view hexChars = "0123456789ABCDEFabcdef";
			if ((str.size() != 6 && str.size() != 8) || str.find_first_not_of(hexChars) != std::string_view::npos) {
				logger::error("[Effect11 PL] invalid color");
				continue;
			}

			try {
				uint32_t color = static_cast<uint32_t>(std::stoul(std::string(str), nullptr, 16));
				data.color = color;
			} catch (const std::exception&) {
				logger::error("[Effect11 PL] invalid color");
				continue;
			}

			const auto filename = GetLowercaseStemFromPath(path, ".ini");
			if (!filename)
				continue;

			if (gradientConfigs.contains(*filename)) {
				logger::warn("[Effect11 PL] Duplicate gradient config '{}'; keeping first, ignoring {}", *filename, path);
				continue;
			}

			logger::debug("[Effect11 PL] Inserting {}", *filename);
			gradientConfigs.emplace(*filename, data);
		}
	}
}
