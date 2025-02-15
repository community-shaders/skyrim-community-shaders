#include "State.h"

#include <codecvt>

#include <pystring/pystring.h>

#include "Deferred.h"
#include "Features/CloudShadows.h"
#include "Features/TerrainBlending.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "Streamline.h"
#include "TruePBR.h"
#include "Upscaling.h"

void State::Draw()
{
	auto shaderCache = globals::shaderCache;
	auto deferred = globals::deferred;
	auto terrainBlending = globals::features::terrainBlending;
	auto cloudShadows = globals::features::cloudShadows;
	auto truePBR = globals::truePBR;
	auto smState = globals::game::smState;
	auto context = globals::d3d::context;

	if (shaderCache->IsEnabled()) {
		if (terrainBlending->loaded)
			terrainBlending->TerrainShaderHacks();

		if (cloudShadows->loaded)
			cloudShadows->SkyShaderHacks();

		truePBR->SetShaderResouces(context);

		if (!deferred->inReflections) {
			if (auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator()) {
				// Set an unused bit to indicate if we are rendering an object in the main rendering passes
				if (accumulator->GetRuntimeData().activeShadowSceneNode == smState->shadowSceneNode[0]) {
					currentExtraDescriptor |= (uint32_t)ExtraShaderDescriptors::InWorld;
				}
			}
		}

		if (deferred->inReflections)
			currentExtraDescriptor |= (uint32_t)ExtraShaderDescriptors::IsReflections;

		if (deferred->inDecals)
			currentExtraDescriptor |= (uint32_t)ExtraShaderDescriptors::IsDecal;

		if (forceUpdatePermutationBuffer || currentPixelDescriptor != lastPixelDescriptor || currentExtraDescriptor != lastExtraDescriptor) {
			PermutationCB data{};
			data.VertexShaderDescriptor = currentVertexDescriptor;
			data.PixelShaderDescriptor = currentPixelDescriptor;
			data.ExtraShaderDescriptor = currentExtraDescriptor;

			permutationCB->Update(data);

			lastVertexDescriptor = currentVertexDescriptor;
			lastPixelDescriptor = currentPixelDescriptor;
			lastExtraDescriptor = currentExtraDescriptor;

			forceUpdatePermutationBuffer = false;
		}

		currentExtraDescriptor = 0;

		if (frameChecker.IsNewFrame()) {
			ID3D11Buffer* buffers[3] = { permutationCB->CB(), sharedDataCB->CB(), featureDataCB->CB() };
			context->PSSetConstantBuffers(4, 3, buffers);
			context->CSSetConstantBuffers(5, 2, buffers + 1);
		}

		if (currentShader && updateShader) {
			auto type = currentShader->shaderType.get();
			if (type == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					deferred->CopyShadowData();
				}
			}

			if (type > 0 && type < RE::BSShader::Type::Total) {
				if (enabledClasses[type - 1]) {
					// Only check against non-shader bits
					currentPixelDescriptor &= ~modifiedPixelDescriptor;

					if (frameAnnotations) {
						BeginPerfEvent(std::format("Draw: CS {}::{:x}::{}", magic_enum::enum_name(currentShader->shaderType.get()), currentPixelDescriptor, currentShader->fxpFilename));
						SetPerfMarker(std::format("Defines: {}", SIE::ShaderCache::GetDefinesString(*currentShader, currentPixelDescriptor)));
						EndPerfEvent();
					}
				}
			}
		}
		updateShader = false;
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
	initialized = false;
	forceUpdatePermutationBuffer = true;
	frameCount++;
}

void State::Setup()
{
	globals::truePBR->SetupResources();
	SetupResources();
	for (auto* feature : Feature::GetFeatureList())
		if (feature->loaded)
			feature->SetupResources();
	globals::deferred->SetupResources();
	globals::streamline->SetupResources();
	if (!upscalerLoaded)
		globals::upscaling->CreateUpscalingResources();
	if (initialized)
		return;
	initialized = true;
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
	ConfigMode configMode = a_configMode;
	auto shaderCache = globals::shaderCache;
	json settings;
	bool errorDetected = false;

	try {
		std::filesystem::create_directories(folderPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Load ({}) : {}\n", folderPath, e.what());
		errorDetected = true;
	}

	// Attempt to load the config file
	auto tryLoadConfig = [&](const std::string& path) {
		std::ifstream i(path);
		logger::info("Attempting to open config file: {}", path);
		if (!i.is_open()) {
			logger::warn("Unable to open config file: {}", path);
			return false;
		}
		try {
			i >> settings;
			i.close();  // Close the file after reading
			return true;
		} catch (const nlohmann::json::parse_error& e) {
			logger::warn("Error parsing json config file ({}) : {}\n", path, e.what());
			i.close();  // Ensure the file is closed even on error
			return false;
		}
	};

	std::string configPath = GetConfigPath(configMode);
	if (!tryLoadConfig(configPath)) {
		logger::info("Unable to open user config file ({}); trying default ({})", configPath, defaultConfigPath);
		configMode = ConfigMode::DEFAULT;
		configPath = GetConfigPath(configMode);

		if (!tryLoadConfig(configPath)) {
			logger::info("No default config ({}), generating new one", configPath);
			std::fill(enabledClasses, enabledClasses + RE::BSShader::Type::Total - 1, true);
			Save(configMode);
			// Attempt to load the newly created config
			configPath = GetConfigPath(configMode);
			if (!tryLoadConfig(configPath)) {
				logger::error("Error opening newly created config file ({})\n", configPath);
				return;  // Exit if the new config can't be opened
			}
		}
	}

	// Proceed with loading settings from the loaded configuration

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
				logLevel = static_cast<spdlog::level::level_enum>((int)advanced["Log Level"]);
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
			for (int classIndex = 0; classIndex < RE::BSShader::Type::Total - 1; ++classIndex) {
				auto name = magic_enum::enum_name((RE::BSShader::Type)(classIndex + 1));
				if (originalShaders[name].is_boolean()) {
					enabledClasses[classIndex] = originalShaders[name];
				} else {
					logger::warn("Invalid entry for shader class '{}', using default", name);
				}
			}
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

		auto upscaling = globals::upscaling;
		auto& upscalingJson = settings[upscaling->GetShortName()];
		if (upscalingJson.is_object()) {
			logger::info("Loading Upscaling settings");
			try {
				upscaling->LoadSettings(upscalingJson);
			} catch (...) {
				logger::warn("Invalid settings for Upscaling, using default.");
				upscaling->RestoreDefaultSettings();
			}
		} else {
			logger::warn("Missing settings for Upscaling, using default.");
		}

		auto streamline = globals::streamline;
		auto& streamlineJson = settings[streamline->GetShortName()];
		if (streamlineJson.is_object()) {
			logger::info("Loading Streamline settings");
			try {
				streamline->LoadSettings(streamlineJson);
			} catch (...) {
				logger::warn("Invalid settings for Streamline, using default.");
				streamline->RestoreDefaultSettings();
			}
		} else {
			logger::warn("Missing settings for Streamline, using default.");
		}

		for (auto* feature : Feature::GetFeatureList()) {
			try {
				const std::string featureName = feature->GetShortName();
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
				if (!isDisabled) {
					logger::info("Loading Feature: '{}'", featureName);
					feature->Load(settings);
				} else {
					logger::info("Feature '{}' is disabled at boot.", featureName);
				}
			} catch (const std::exception& e) {
				feature->failedLoadedMessage = std::format(
					"{}{} failed to load. Check CommunityShaders.log",
					feature->failedLoadedMessage.empty() ? "" : feature->failedLoadedMessage + "\n",
					feature->GetName());
				logger::warn("Error loading setting for feature '{}': {}", feature->GetShortName(), e.what());
			}
		}
		if (settings["Version"].is_string() && settings["Version"].get<std::string>() != Plugin::VERSION.string()) {
			logger::info("Found older config for version {}; upgrading to {}", (std::string)settings["Version"], Plugin::VERSION.string());
			Save(configMode);
		}
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

	auto upscaling = globals::upscaling;
	auto& upscalingJson = settings[upscaling->GetShortName()];
	upscaling->SaveSettings(upscalingJson);

	auto streamline = globals::streamline;
	auto& streamlineJson = settings[streamline->GetShortName()];
	streamline->SaveSettings(streamlineJson);

	json originalShaders;
	for (int classIndex = 0; classIndex < RE::BSShader::Type::Total - 1; ++classIndex) {
		originalShaders[magic_enum::enum_name((RE::BSShader::Type)(classIndex + 1))] = enabledClasses[classIndex];
	}
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

void State::PostPostLoad()
{
	upscalerLoaded = GetModuleHandle(L"Data\\SKSE\\Plugins\\SkyrimUpscaler.dll");
	if (upscalerLoaded)
		logger::info("Skyrim Upscaler detected");
	else
		logger::info("Skyrim Upscaler not detected");
	// No hooks should be here, hook in XSEPlugin::MessageHandler()
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
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), static_cast<int>(logLevel));
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
	auto index = static_cast<uint32_t>(a_type) + 1;
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

				static auto enableImprovedSnow = RE::GetINISetting("bEnableImprovedSnow:Display");
				static bool vr = REL::Module::IsVR();

				if (vr || !enableImprovedSnow->GetBool())
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

void State::UpdateSharedData(bool a_inWorld)
{
	{
		SharedDataCB data{};

		const auto shaderManager = globals::game::smState;
		const RE::NiTransform& dalcTransform = shaderManager->directionalAmbientTransform;
		Util::StoreTransform3x4NoScale(data.DirectionalAmbient, dalcTransform);

		auto shadowSceneNode = shaderManager->shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		data.DirLightColor = { dirLight->GetLightRuntimeData().diffuse.red, dirLight->GetLightRuntimeData().diffuse.green, dirLight->GetLightRuntimeData().diffuse.blue, 1.0f };

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		data.DirLightColor *= !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;

		const auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { screenSize.x, screenSize.y, 1.0f / screenSize.x, 1.0f / screenSize.y };
		data.Timer = timer;

		auto bTAA = !globals::game::isVR ? imageSpaceManager->GetRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled :
		                                   imageSpaceManager->GetVRRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled;

		data.FrameCount = frameCount * (bTAA || globals::state->upscalerLoaded);
		data.FrameCountAlwaysActive = frameCount;

		if (a_inWorld) {
			for (int i = -2; i <= 2; i++) {
				for (int k = -2; k <= 2; k++) {
					int waterTile = (i + 2) + ((k + 2) * 5);
					data.WaterData[waterTile] = Util::TryGetWaterData((float)i * 4096.0f, (float)k * 4096.0f);
				}
			}
		}

		if (auto sky = globals::game::sky) {
			data.InInterior = sky->mode.get() != RE::Sky::Mode::kFull;
			data.HideSky = !data.InInterior && sky->flags.any(RE::Sky::Flags::kHideSky);
		} else {
			data.InInterior = true;
			data.HideSky = true;
		}

		if (auto ui = globals::game::ui)
			data.InMapMenu = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
		else
			data.InMapMenu = true;

		sharedDataCB->Update(data);
	}

	{
		auto [data, size] = GetFeatureBufferData(a_inWorld);

		featureDataCB->Update(data, size);

		delete[] data;
	}

	const auto& depth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto terrainBlending = globals::features::terrainBlending;
	auto srv = (terrainBlending->loaded ? terrainBlending->blendedDepthTexture16->srv.get() : depth.depthSRV);

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
