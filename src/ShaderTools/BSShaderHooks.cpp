#include "BSShaderHooks.h"

namespace BSShaderHooks
{
	void hk_LoadShaders(RE::BSShader* bsShader, std::uintptr_t)
	{
		const std::string_view loaderType = bsShader->fxpFilename ? bsShader->fxpFilename : "Unknown";
		logger::info("BSShader::LoadShaders called on {} - ps count {}", loaderType, bsShader->pixelShaders.size());

		if (loaderType == "Lighting") {
			std::unordered_map<std::uint32_t, std::wstring> techniqueFileMap;

			const auto shaderDir = std::filesystem::current_path() /= "Data/SKSE/plugins/shaders/"sv;

			if (std::filesystem::exists(shaderDir)) {
				std::size_t foundCount = 0;
				std::size_t successCount = 0;
				std::size_t failedCount = 0;

				for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
					if (entry.path().extension().generic_string() != ".hlsl"sv)
						continue;

					auto filenameStr = entry.path().filename().string();
					auto techniqueIdStr = filenameStr.substr(0, filenameStr.find('_'));
					const std::uint32_t techniqueId = std::strtoul(techniqueIdStr.c_str(), nullptr, 16);
					logger::info("found shader technique id {:08x} with path {}", techniqueId, entry.path().generic_string());
					foundCount++;
					techniqueFileMap.insert(std::make_pair(techniqueId, absolute(entry.path()).wstring()));
				}

				for (const auto& entry : bsShader->pixelShaders) {
					auto tFileIt = techniqueFileMap.find(entry->id);
					if (tFileIt != techniqueFileMap.end()) {
						if (const auto shader = ShaderCompiler::CompileAndRegisterPixelShader(tFileIt->second)) {
							logger::info("shader compiled successfully, replacing old shader");
							successCount++;
							entry->shader = reinterpret_cast<REX::W32::ID3D11PixelShader*>(shader);
						} else {
							failedCount++;
						}
					}
				}
			}
		}

		std::unordered_map<std::uint32_t, std::wstring> techniqueFileMap;

		const auto shaderDir = std::filesystem::current_path() /= std::format("Data\\Shaders\\{}\\"sv, loaderType);
		if (std::filesystem::exists(shaderDir)) {
			logger::info("{}", shaderDir.generic_string());

			std::size_t foundCount = 0;
			std::size_t successCount = 0;
			std::size_t failedCount = 0;

			for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
				std::string fileStr = entry.path().filename().generic_string();

				std::string techniqueIDStr;
				std::uint32_t techniqueId;
				if (fileStr.ends_with(".ps.hlsl")) {
					techniqueIDStr = fileStr.substr(0, fileStr.length() - 8);
					techniqueId = std::strtoul(techniqueIDStr.c_str(), nullptr, 16);
					auto tFileIt = techniqueFileMap.find(techniqueId);
					if (tFileIt != techniqueFileMap.end() && !tFileIt->second.ends_with(L".hlsl"))
						continue;  // favour compiled binary blobs
				} else if (fileStr.ends_with(".ps")) {
					techniqueIDStr = fileStr.substr(0, fileStr.length() - 3);
					techniqueId = std::strtoul(techniqueIDStr.c_str(), nullptr, 16);
				} else {
					continue;
				}

				logger::info("found shader technique id {:08x} with path {}", techniqueId, entry.path().generic_string());
				foundCount++;
				techniqueFileMap.insert_or_assign(techniqueId, absolute(entry.path()).wstring());
			}

			for (const auto& entry : bsShader->pixelShaders) {
				auto tFileIt = techniqueFileMap.find(entry->id);
				if (tFileIt != techniqueFileMap.end()) {
					bool compile = tFileIt->second.ends_with(L".hlsl");
					if (const auto shader = compile ? ShaderCompiler::CompileAndRegisterPixelShader(tFileIt->second) : ShaderCompiler::RegisterPixelShader(tFileIt->second)) {
						logger::info("shader compiled successfully, replacing old shader");
						successCount++;
						entry->shader = reinterpret_cast<REX::W32::ID3D11PixelShader*>(shader);
					} else {
						failedCount++;
					}
				}
			}

			logger::info("found shaders: {} successfully replaced: {} failed to replace: {}", foundCount, successCount, failedCount);
		}
	}
}
