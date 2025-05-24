#include "Features/LightLimitFix/ParticleLights.h"

#include <numbers>

void ParticleLights::GetConfigs()
{
	if (std::filesystem::exists("Data\\ParticleLights")) {
		logger::info("[LLF] Loading particle lights configs");

		auto configs = clib_util::distribution::get_configs("Data\\ParticleLights", "", ".ini");

		if (configs.empty()) {
			logger::warn("[LLF] No .ini files were found within the Data\\ParticleLights folder, aborting...");
			return;
		}

		logger::info("[LLF] {} matching inis found", configs.size());

		for (auto& path : configs) {
			logger::info("[LLF] loading ini : {}", path);

			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetMultiKey();

			if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
				logger::error("\t\t[LLF] couldn't read INI");
				continue;
			}

			Config data{};
			particleLightConfigs.insert({ "default", data });

			data.cull = ini.GetBoolValue("Light", "Cull", false);
			data.colorMult.red = (float)ini.GetDoubleValue("Light", "ColorMultRed", 1.0);
			data.colorMult.green = (float)ini.GetDoubleValue("Light", "ColorMultGreen", 1.0);
			data.colorMult.blue = (float)ini.GetDoubleValue("Light", "ColorMultBlue", 1.0);
			data.radiusMult = (float)ini.GetDoubleValue("Light", "RadiusMult", 1.0);
			data.saturationMult = (float)ini.GetDoubleValue("Light", "SaturationMult", 1.0);

			auto lastSeparatorPos = path.find_last_of("\\/");
			if (lastSeparatorPos != std::string::npos) {
				std::string filename = path.substr(lastSeparatorPos + 1);
				if (filename.size() < 4) {
					logger::error("[LLF] Path too short");
					continue;
				}

				filename.erase(filename.length() - 4);  // Remove ".ini"
				std::transform(filename.begin(), filename.end(), filename.begin(), [](auto c) { return (char)::tolower(c); });

				logger::debug("[LLF] Inserting {}", filename);

				particleLightConfigs.insert({ filename, data });
			} else {
				logger::error("[LLF] Path incomplete");
			}
		}
	}

	if (std::filesystem::exists("Data\\ParticleLights\\Gradients")) {
		logger::info("[LLF] Loading particle lights gradients configs");

		auto configs = clib_util::distribution::get_configs("Data\\ParticleLights\\Gradients", "", ".ini");

		if (configs.empty()) {
			logger::warn("[LLF] No .ini files were found within the Data\\ParticleLights\\Gradients folder, aborting...");
			return;
		}

		logger::info("[LLF] {} matching inis found", configs.size());

		for (auto& path : configs) {
			logger::info("[LLF] loading ini : {}", path);

			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetMultiKey();

			if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
				logger::error("\t\t[LLF] couldn't read INI");
				continue;
			}

			GradientConfig data{};
			const char* value = nullptr;
			constexpr std::string_view prefix1 = "0x";
			constexpr std::string_view prefix2 = "#";
			constexpr std::string_view cset = "0123456789ABCDEFabcdef";

			value = ini.GetValue("Gradient", "Color");
			if (value && strcmp(value, "") != 0) {
				std::string_view str = value;

				if (str.starts_with(prefix1)) {
					str.remove_prefix(prefix1.size());
				}

				if (str.starts_with(prefix2)) {
					str.remove_prefix(prefix2.size());
				}

				bool matches = std::strspn(str.data(), cset.data()) == str.size();

				if (matches) {
					uint32_t color = std::stoi(str.data(), 0, 16);
					data.color = color;
				} else {
					logger::error("[LLF] invalid color");
					continue;
				}
			} else {
				logger::error("[LLF] missing color");
				continue;
			}

			auto lastSeparatorPos = path.find_last_of("\\/");
			if (lastSeparatorPos != std::string::npos) {
				std::string filename = path.substr(lastSeparatorPos + 1);
				if (filename.size() < 4) {
					logger::error("[LLF] Path too short");
					continue;
				}

				filename.erase(filename.length() - 4);  // Remove ".ini"
				std::transform(filename.begin(), filename.end(), filename.begin(), [](auto c) { return (char)::tolower(c); });

				logger::debug("[LLF] Inserting {}", filename);

				particleLightGradientConfigs.insert({ filename, data });
			} else {
				logger::error("[LLF] Path incomplete");
			}
		}
	}
}
