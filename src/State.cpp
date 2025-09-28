#include "State.h"

#include <codecvt>

#include <pystring/pystring.h>

#include "Deferred.h"
#include "FeatureIssues.h"
#include "Features/CloudShadows.h"
#include "Features/PerformanceOverlay.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Menu.h"
#include "SettingsOverrideManager.h"
#include "ShaderCache.h"
#include "TruePBR.h"

void State::Draw()
{
	auto shaderCache = globals::shaderCache;
	auto deferred = globals::deferred;
	auto& terrainBlending = globals::features::terrainBlending;
	auto& terrainHelper = globals::features::terrainHelper;
	auto& cloudShadows = globals::features::cloudShadows;
	auto truePBR = globals::truePBR;
	auto context = globals::d3d::context;

	if (shaderCache->IsEnabled()) {
		if (terrainBlending.loaded)
			terrainBlending.TerrainShaderHacks();

		if (cloudShadows.loaded)
			cloudShadows.SkyShaderHacks();

		if (terrainHelper.loaded)
			terrainHelper.SetShaderResouces(context);

		truePBR->SetShaderResouces(context);

		if (permutationData != permutationDataPrevious) {
			permutationCB->Update(permutationData);
			permutationDataPrevious = permutationData;
		}

		if (currentShader && updateShader) {
			if (currentShader->shaderType.get() == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					deferred->CopyShadowData();
				}
			}
		}

		if (globals::menu->overlayVisible && globals::features::performanceOverlay.loaded && globals::features::performanceOverlay.IsOverlayVisible())
			Debug();

		updateShader = false;
	}
}

void State::Debug()
{
	auto lock = Lock();

	if (frameChecker.IsNewFrame()) {
		// Smooth draw calls and frame times for all shader types
		for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) + 1; ++i) {
			smoothDrawCalls[i] = smoothDrawCalls[i] * static_cast<float>(0.95) + drawCalls[i] * static_cast<float>(0.05);
			smoothFrameTimePerType[i] = smoothFrameTimePerType[i] * static_cast<float>(0.95) + frameTimePerType[i] * static_cast<float>(0.05);
		}
		// Reset counters for next frame
		for (auto& c : drawCalls)
			c = 0;
		for (auto& ft : frameTimePerType)
			ft = 0.0f;

		// Start timing for this frame
		if (frameTimingFrequency.QuadPart == 0) {
			QueryPerformanceFrequency(&frameTimingFrequency);
		}
		QueryPerformanceCounter(&frameStartTime);
		frameTimingActive = true;
	}

	// Track time for current shader type if timing is active
	if (frameTimingActive && currentShader) {
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);

		// Calculate elapsed time in milliseconds
		float elapsed = (currentTime.QuadPart - frameStartTime.QuadPart) * 1000.0f / frameTimingFrequency.QuadPart;

		// Add elapsed time to the current shader type
		frameTimePerType[magic_enum::enum_integer(currentShader->shaderType.get())] += elapsed;
		frameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] += elapsed;

		// Update start time for next measurement
		frameStartTime = currentTime;
	}

	if (currentShader) {
		drawCalls[magic_enum::enum_integer(currentShader->shaderType.get())]++;
		drawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]++;
	}

	if (currentShader && updateShader && frameAnnotations) {
		BeginPerfEvent(std::format("Draw: CS {}::{:x}::{}", magic_enum::enum_name(currentShader->shaderType.get()), permutationData.PixelShaderDescriptor, currentShader->fxpFilename));
		SetPerfMarker(std::format("Defines: {}", SIE::ShaderCache::GetDefinesString(*currentShader, permutationData.PixelShaderDescriptor)));
		EndPerfEvent();
	}
}

void State::Reset()
{
	for (auto* feature : Feature::GetFeatureList())
		if (feature->loaded)
			feature->Reset();
	if (!globals::game::ui->GameIsPaused())
		timer += RE::GetSecondsSinceLastFrame();
	lastModifiedPixelDescriptor = 0;
	lastModifiedVertexDescriptor = 0;
	lastPixelDescriptor = 0;
	lastVertexDescriptor = 0;
	std::memset(&permutationDataPrevious, 0xFF, sizeof(PermutationCB));
	frameCount++;

	if (auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton()) {
		GET_INSTANCE_MEMBER(BSImagespaceShaderApplyReflections, imageSpaceManager);

		// Disable reflections being applied to things other than water
		if (BSImagespaceShaderApplyReflections.get()) {
			BSImagespaceShaderApplyReflections->active = false;
		}
	}

	// Disable "improved" snow shader, unsupported
	if (!globals::game::isVR) {
		RE::GetINISetting("bEnableImprovedSnow:Display")->data.b = false;
	}

	activeReflections = false;
}

void State::Setup()
{
	globals::truePBR->SetupResources();
	SetupResources();
	for (auto* feature : Feature::GetFeatureList())
		if (feature->loaded)
			feature->SetupResources();
	globals::deferred->SetupResources();
}

static const std::string& GetConfigPath(State::ConfigMode a_configMode)
{
	switch (a_configMode) {
	case State::ConfigMode::USER:
		return globals::state->userConfigPath;
	case State::ConfigMode::TEST:
		return globals::state->testConfigPath;
	case State::ConfigMode::DEFAULT:
	default:
		return globals::state->defaultConfigPath;
	}
}

void State::Load(ConfigMode a_configMode, bool a_allowReload)
{
	auto shaderCache = globals::shaderCache;
	json settings;
	bool errorDetected = false;

	auto configFolderPath = std::filesystem::path(GetConfigPath(a_configMode)).parent_path().string();
	auto defaultConfigFilePath = GetConfigPath(ConfigMode::DEFAULT);
	auto userConfigFilePath = GetConfigPath(ConfigMode::USER);

	try {
		std::filesystem::create_directories(configFolderPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Load ({}) : {}\n", configFolderPath, e.what());
		errorDetected = true;
	}

	// Attempt to load the config file
	auto tryLoadConfig = [&](const std::string& path) -> bool {
		std::ifstream i(path);
		logger::info("Attempting to open config file: {}", path);
		if (!i.is_open()) {
			logger::warn("Unable to open config file: {}", path);
			return false;
		}
		try {
			i >> settings;
			i.close();
			return true;
		} catch (const nlohmann::json::parse_error& e) {
			logger::warn("Error parsing json config file ({}) : {}\n", path, e.what());
			i.close();
			return false;
		}
	};

	// NEW LOADING ORDER: Default → Overrides → User

	// Step 1: Always start with default settings
	logger::info("Loading default settings from: {}", defaultConfigFilePath);
	if (!tryLoadConfig(defaultConfigFilePath)) {
		logger::info("No default config ({}), generating new one", defaultConfigFilePath);
		std::fill(enabledClasses, enabledClasses + magic_enum::enum_integer(RE::BSShader::Type::Total) - 1, true);
		Save(ConfigMode::DEFAULT);
		// Attempt to load the newly created config
		if (!tryLoadConfig(defaultConfigFilePath)) {
			logger::error("Error opening newly created default config file ({})\n", defaultConfigFilePath);
			return;
		}
	}

	// Step 2: Apply overrides (only new/changed ones) to default settings
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	size_t overridesDiscovered = overrideManager->DiscoverOverrides();
	json appliedOverrides = overrideManager->LoadAppliedOverridesTracking();

	if (overridesDiscovered > 0) {
		logger::info("Discovered {} override files", overridesDiscovered);

		// Apply global overrides to main settings (only new/changed ones)
		size_t newGlobalOverrides = overrideManager->ApplyNewOverrides(settings, appliedOverrides);
		if (newGlobalOverrides > 0) {
			logger::info("Applied {} new/changed global override(s)", newGlobalOverrides);
		}
	}

	// Step 3: Apply user settings on top of default + overrides
	if (a_configMode == ConfigMode::USER) {
		json userSettings;
		std::ifstream userFile(userConfigFilePath);
		if (userFile.is_open()) {
			try {
				userFile >> userSettings;
				userFile.close();

				// Merge user settings on top of (default + overrides)
				for (auto& [key, value] : userSettings.items()) {
					settings[key] = value;
				}
				logger::info("Applied user settings from: {}", userConfigFilePath);
			} catch (const nlohmann::json::parse_error& e) {
				logger::warn("Error parsing user config file: {}", e.what());
				userFile.close();
			}
		} else {
			logger::info("No user config file found at: {}", userConfigFilePath);
		}
	}

	try {
		// Load Menu settings

		if (settings["Menu"].is_object()) {
			logger::info("Loading 'Menu' settings");
			globals::menu->Load(settings["Menu"]);
		}

		if (settings["Advanced"].is_object()) {
			logger::info("Loading 'Advanced' settings");
			json& advanced = settings["Advanced"];
			if (advanced["Dump Shaders"].is_boolean())
				shaderCache->SetDump(advanced["Dump Shaders"]);
			if (advanced["Log Level"].is_number_integer())
				logLevel = magic_enum::enum_cast<spdlog::level::level_enum>(advanced["Log Level"].get<int>()).value_or(spdlog::level::info);
			if (advanced["Shader Defines"].is_string())
				SetDefines(advanced["Shader Defines"]);
			if (advanced["Compiler Threads"].is_number_integer())
				shaderCache->compilationThreadCount = std::clamp(advanced["Compiler Threads"].get<int32_t>(), 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
			if (advanced["Background Compiler Threads"].is_number_integer())
				shaderCache->backgroundCompilationThreadCount = std::clamp(advanced["Background Compiler Threads"].get<int32_t>(), 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
			if (advanced["Use FileWatcher"].is_boolean())
				shaderCache->SetFileWatcher(advanced["Use FileWatcher"]);
			if (advanced["Frame Annotations"].is_boolean())
				frameAnnotations = advanced["Frame Annotations"];
		}

		if (settings["General"].is_object()) {
			logger::info("Loading 'General' settings");
			json& general = settings["General"];

			if (general["Enable Shaders"].is_boolean())
				shaderCache->SetEnabled(general["Enable Shaders"]);

			if (general["Enable Disk Cache"].is_boolean())
				shaderCache->SetDiskCache(general["Enable Disk Cache"]);

			if (general["Enable Async"].is_boolean())
				shaderCache->SetAsync(general["Enable Async"]);
		}

		if (settings["Replace Original Shaders"].is_object()) {
			logger::info("Loading 'Replace Original Shaders' settings");
			json& originalShaders = settings["Replace Original Shaders"];
			ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
				auto name = magic_enum::enum_name(type);
				if (originalShaders[name].is_boolean()) {
					enabledClasses[classIndex] = originalShaders[name];
				} else {
					logger::warn("Invalid entry for shader class '{}', using default", name);
				}
			});
		}
		// Ensure 'Disable at Boot' section exists in the JSON
		if (!settings.contains("Disable at Boot") || !settings["Disable at Boot"].is_object()) {
			// Initialize to an empty object if it doesn't exist
			settings["Disable at Boot"] = json::object();
		}

		json& disabledFeaturesJson = settings["Disable at Boot"];
		logger::info("Loading 'Disable at Boot' settings");

		for (auto& [featureName, featureStatus] : disabledFeaturesJson.items()) {
			if (featureStatus.is_boolean()) {
				disabledFeatures[featureName] = featureStatus.get<bool>();
			} else {
				logger::warn("Invalid entry for feature '{}' in 'Disable at Boot', expected boolean.", featureName);
			}
		}
		for (const auto& [featureName, _] : specialFeatures) {
			if (IsFeatureDisabled(featureName)) {
				logger::info("Special Feature '{}' disabled at boot", featureName);
			}
		}
		for (auto* feature : Feature::GetFeatureList()) {
			try {
				const std::string featureName = feature->GetShortName();
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
				if (!isDisabled) {
					logger::info("Loading Feature: '{}'", featureName);

					// Load base feature settings from merged config (default + user)
					feature->Load(settings);

					// Apply new/changed feature-specific overrides if any
					if (overridesDiscovered > 0) {
						json featureJson;
						feature->SaveSettings(featureJson);  // Get current settings as JSON

						size_t newFeatureOverrides = overrideManager->ApplyNewFeatureOverrides(featureName, featureJson, appliedOverrides);
						if (newFeatureOverrides > 0) {
							logger::info("Applied {} new/changed override(s) to {}", newFeatureOverrides, feature->GetName());
							try {
								feature->LoadSettings(featureJson);  // Reload with new overrides applied
							} catch (...) {
								logger::warn("Invalid override settings for {}, keeping original settings.", feature->GetName());
							}
						}
					}
				} else {
					logger::info("Feature '{}' is disabled at boot.", featureName);
				}
			} catch (const std::exception& e) {
				feature->failedLoadedMessage = feature->failedLoadedMessage.empty() ?
				                                   (feature->GetName() + " failed to load. Check CommunityShaders.log") :
				                                   (feature->failedLoadedMessage + "\n" + feature->GetName() + " failed to load. Check CommunityShaders.log");
				logger::warn("Error loading setting for feature '{}': {}", feature->GetShortName(), e.what());
			}
		}

		// Save updated applied overrides tracking
		if (overridesDiscovered > 0) {
			overrideManager->SaveAppliedOverridesTracking(appliedOverrides);
		}

		if (settings["Version"].is_string() && settings["Version"].get<std::string>() != Plugin::VERSION.string()) {
			logger::info("Found older config for version {}; upgrading to {}", (std::string)settings["Version"], Plugin::VERSION.string());
			Save(a_configMode);  // Use original config mode
		}

		FeatureIssues::ScanForOrphanedFeatureINIs();

		logger::info("Loading Settings Complete");
	} catch (const json::exception& e) {
		logger::info("General JSON error accessing settings: {}; recreating config", e.what());
		Save(a_configMode);
		errorDetected = true;
	} catch (const std::exception& e) {
		logger::info("General error accessing settings: {}; recreating config", e.what());
		Save(a_configMode);
		errorDetected = true;
	}
	if (errorDetected && a_allowReload)
		Load(a_configMode, false);
}

void State::Save(ConfigMode a_configMode)
{
	const auto shaderCache = globals::shaderCache;
	std::string configPath = GetConfigPath(a_configMode);
	std::ofstream o{ configPath };

	try {
		std::filesystem::create_directories(folderPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Save ({}) : {}\n", folderPath, e.what());
		return;
	}

	// Check if the file opened successfully
	if (!o.is_open()) {
		logger::warn("Failed to open config file for saving: {}", configPath);
		return;  // Exit early if file cannot be opened
	}

	json settings;

	globals::menu->Save(settings["Menu"]);

	json advanced;
	advanced["Dump Shaders"] = shaderCache->IsDump();
	advanced["Log Level"] = logLevel;
	advanced["Shader Defines"] = shaderDefinesString;
	advanced["Compiler Threads"] = shaderCache->compilationThreadCount;
	advanced["Background Compiler Threads"] = shaderCache->backgroundCompilationThreadCount;
	advanced["Use FileWatcher"] = shaderCache->UseFileWatcher();
	advanced["Frame Annotations"] = frameAnnotations;
	settings["Advanced"] = advanced;

	json general;
	general["Enable Shaders"] = shaderCache->IsEnabled();
	general["Enable Disk Cache"] = shaderCache->IsDiskCache();
	general["Enable Async"] = shaderCache->IsAsync();

	settings["General"] = general;

	auto& upscaling = globals::features::upscaling;
	auto& upscalingJson = settings[upscaling.GetShortName()];
	upscaling.SaveSettings(upscalingJson);

	json originalShaders;
	ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
		originalShaders[magic_enum::enum_name(type)] = enabledClasses[classIndex];
	});
	settings["Replace Original Shaders"] = originalShaders;

	json disabledFeaturesJson;
	for (const auto& [featureName, isDisabled] : disabledFeatures) {
		disabledFeaturesJson[featureName] = isDisabled;
	}
	settings["Disable at Boot"] = disabledFeaturesJson;

	settings["Version"] = Plugin::VERSION.string();

	for (auto* feature : Feature::GetFeatureList())
		feature->Save(settings);

	try {
		o << settings.dump(1);
		logger::info("Saving settings to {}", configPath);
	} catch (const std::exception& e) {
		logger::warn("Failed to write settings to file: {}. Error: {}", configPath, e.what());
	}
}

bool State::ValidateCache(CSimpleIniA& a_ini)
{
	bool valid = true;
	for (auto* feature : Feature::GetFeatureList())
		valid = valid && feature->ValidateCache(a_ini);
	return valid;
}

void State::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	for (auto* feature : Feature::GetFeatureList())
		feature->WriteDiskCacheInfo(a_ini);
}

void State::SetLogLevel(spdlog::level::level_enum a_level)
{
	logLevel = a_level;
	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), magic_enum::enum_integer(logLevel));
}

spdlog::level::level_enum State::GetLogLevel()
{
	return logLevel;
}

void State::SetDefines(std::string a_defines)
{
	shaderDefines.clear();
	shaderDefinesString = "";
	std::string name = "";
	std::string definition = "";
	auto defines = pystring::split(a_defines, ";");
	for (const auto& define : defines) {
		auto cleanedDefine = pystring::strip(define);
		auto token = pystring::split(cleanedDefine, "=");
		if (token.empty() || token[0].empty())
			continue;
		if (token.size() > 2) {
			logger::warn("Define string has too many '='; ignoring {}", define);
			continue;
		}
		name = pystring::strip(token[0]);
		if (token.size() == 2) {
			definition = pystring::strip(token[1]);
		}
		shaderDefinesString += pystring::strip(define) + ";";
		shaderDefines.push_back(std::pair(name, definition));
	}
	shaderDefinesString = shaderDefinesString.substr(0, shaderDefinesString.size() - 1);
	logger::debug("Shader Defines set to {}", shaderDefinesString);
}

std::vector<std::pair<std::string, std::string>>* State::GetDefines()
{
	return &shaderDefines;
}

bool State::ShaderEnabled(const RE::BSShader::Type a_type)
{
	auto index = magic_enum::enum_integer(a_type) + 1;
	if (index < sizeof(enabledClasses)) {
		return enabledClasses[index];
	}
	return false;
}

bool State::IsShaderEnabled(const RE::BSShader& a_shader)
{
	return ShaderEnabled(a_shader.shaderType.get());
}

bool State::IsDeveloperMode()
{
	return GetLogLevel() <= spdlog::level::debug;
}

void State::ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
{
	a_properties->supportUnorderedAccess = true;
	logger::debug("Adding UAV access to {}", magic_enum::enum_name(a_target));
}

void State::SetupResources()
{
	for (auto& c : drawCalls)
		c = 0;
	for (auto& c : smoothDrawCalls)
		c = 0;
	for (auto& ft : frameTimePerType)
		ft = 0.0f;
	for (auto& sft : smoothFrameTimePerType)
		sft = 0.0f;

	frameTimingActive = false;

	auto renderer = globals::game::renderer;

	permutationCB = new ConstantBuffer(ConstantBufferDesc<PermutationCB>());
	sharedDataCB = new ConstantBuffer(ConstantBufferDesc<SharedDataCB>());

	auto [data, size] = GetFeatureBufferData(false);
	featureDataCB = new ConstantBuffer(ConstantBufferDesc((uint32_t)size));
	delete[] data;

	// Grab main texture to get resolution
	// VR cannot use viewport->screenWidth/Height as it's the desktop preview window's resolution and not HMD
	D3D11_TEXTURE2D_DESC texDesc{};
	renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&texDesc);

	screenSize = { (float)texDesc.Width, (float)texDesc.Height };
	globals::d3d::context->QueryInterface(__uuidof(pPerf), reinterpret_cast<void**>(&pPerf));

	featureLevel = globals::d3d::device->GetFeatureLevel();

	tracyCtx = TracyD3D11Context(globals::d3d::device, globals::d3d::context);
}

void State::ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred)
{
	auto deferred = globals::deferred;

	if (a_shader.shaderType.get() != RE::BSShader::Type::Utility && a_shader.shaderType.get() != RE::BSShader::Type::ImageSpace) {
		switch (a_shader.shaderType.get()) {
		case RE::BSShader::Type::Lighting:
			{
				a_vertexDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::RimLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::SoftLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BackLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Specular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AnisoLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::TruePbr);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow);
				if (a_pixelDescriptor & (uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask) {
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest;
					a_pixelDescriptor &= ~(uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask;
				}

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow);

				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::Deferred;

				{
					uint32_t technique = 0x3F & (a_vertexDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Parallax ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Facegen ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::FacegenRGBTint ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjects ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjectHD ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::MultiIndexSparkle ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Hair)
						a_vertexDescriptor &= ~(0x3F << 24);
				}

				{
					uint32_t technique = 0x3F & (a_pixelDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap)
						a_pixelDescriptor &= ~(0x3F << 24);
				}
			}
			break;
		case RE::BSShader::Type::Water:
			{
				auto flags = ~((uint32_t)SIE::ShaderCache::WaterShaderFlags::Reflections |
							   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Cubemap |
							   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Interior);
				a_vertexDescriptor &= flags;
				a_pixelDescriptor &= flags;
			}
			break;
		case RE::BSShader::Type::Effect:
			{
				auto flags = ~((uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor |
							   (uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToAlpha |
							   (uint32_t)SIE::ShaderCache::EffectShaderFlags::IgnoreTexAlpha);
				a_vertexDescriptor &= flags;
				a_pixelDescriptor &= flags;

				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::EffectShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::DistantTree:
			{
				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::DistantTreeShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::Sky:
			{
				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= 256;
			}
			break;
		case RE::BSShader::Type::Grass:
			{
				auto technique = a_vertexDescriptor & 0xF;
				auto flags = a_vertexDescriptor & ~0xF;
				if (technique == static_cast<uint32_t>(SIE::ShaderCache::GrassShaderTechniques::TruePbr)) {
					technique = 0;
				}
				a_vertexDescriptor = flags | technique;
			}
			break;
		}
	}
}

void State::BeginPerfEvent(std::string_view title)
{
	pPerf->BeginEvent(std::wstring(title.begin(), title.end()).c_str());
}

void State::EndPerfEvent()
{
	pPerf->EndEvent();
}

void State::SetPerfMarker(std::string_view title)
{
	pPerf->SetMarker(std::wstring(title.begin(), title.end()).c_str());
}

void State::SetAdapterDescription(const std::wstring& description)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	adapterDescription = converter.to_bytes(description);
}

void State::UpdateSharedData([[maybe_unused]] bool a_inWorld, [[maybe_unused]] bool a_prepass)
{
	{
		SharedDataCB data{};

		const auto shaderManager = globals::game::smState;
		const RE::NiTransform& dalcTransform = shaderManager->directionalAmbientTransform;
		Util::StoreTransform3x4NoScale(data.DirectionalAmbient, dalcTransform);

		auto shadowSceneNode = shaderManager->shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& lightRuntimeData = dirLight->GetLightRuntimeData();
		data.DirLightColor = { lightRuntimeData.diffuse.red, lightRuntimeData.diffuse.green, lightRuntimeData.diffuse.blue, 1.0f };
		data.DirLightColor *= lightRuntimeData.fade;

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		data.DirLightColor *= !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;

		const auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { screenSize.x, screenSize.y, 1.0f / screenSize.x, 1.0f / screenSize.y };
		data.Timer = timer;

		auto temporal = Util::GetTemporal();

		data.FrameCount = frameCount * temporal;
		data.FrameCountAlwaysActive = frameCount;

		if (a_inWorld) {
			for (int i = -2; i <= 2; i++) {
				for (int k = -2; k <= 2; k++) {
					int waterTile = (i + 2) + ((k + 2) * 5);
					data.WaterData[waterTile] = Util::TryGetWaterData((float)i * 4096.0f, (float)k * 4096.0f);
				}
			}
		}

		data.InInterior = Util::IsInterior();

		if (globals::game::sky)
			data.HideSky = globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
		else
			data.HideSky = false;

		if (globals::game::ui)
			data.InMapMenu = globals::game::ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
		else
			data.InMapMenu = false;

		auto& upscaling = globals::features::upscaling;

		if (upscaling.loaded) {
			auto upscaleMethod = upscaling.GetUpscaleMethod();
			if (temporal && upscaleMethod != Upscaling::UpscaleMethod::kTAA) {
				auto renderSize = Util::ConvertToDynamic(screenSize, true);
				data.MipBias = std::log2f(renderSize.x / screenSize.x) - 1.0f;
			} else {
				data.MipBias = 0;
			}
		} else {
			data.MipBias = 0;
		}

		sharedDataCB->Update(data);
	}

	{
		auto [data, size] = GetFeatureBufferData(a_inWorld);

		featureDataCB->Update(data, size);

		delete[] data;
	}

	const auto& depth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& terrainBlending = globals::features::terrainBlending;
	auto srv = (terrainBlending.loaded ? terrainBlending.blendedDepthTexture16->srv.get() : depth.depthSRV);

	globals::d3d::context->PSSetShaderResources(17, 1, &srv);
}

void State::ClearDisabledFeatures()
{
	disabledFeatures.clear();
}

bool State::SetFeatureDisabled(const std::string& featureName, bool isDisabled)
{
	bool wasPreviouslyDisabled = disabledFeatures.count(featureName) > 0 ? disabledFeatures[featureName] : false;  // Properly check if it exists
	disabledFeatures[featureName] = isDisabled;

	// Log the change
	if (wasPreviouslyDisabled != isDisabled) {
		logger::info("Set feature '{}' to: {}", featureName, isDisabled ? "Disabled" : "Enabled");
	} else {
		logger::info("Feature '{}' state remains: {}", featureName, isDisabled ? "Disabled" : "Enabled");
	}

	return disabledFeatures[featureName];  // Return the current state instead of the input parameter
}

bool State::IsFeatureDisabled(const std::string& featureName)
{
	return disabledFeatures.contains(featureName) && disabledFeatures[featureName];
}

std::unordered_map<std::string, bool>& State::GetDisabledFeatures()
{
	return disabledFeatures;
}

// --- Utility Method Implementations ---

float State::GetTotalSmoothedDrawCalls() const
{
	return static_cast<float>(smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
}
