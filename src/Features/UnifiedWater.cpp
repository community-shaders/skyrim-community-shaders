#include "UnifiedWater.h"

#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "PCH.h"
#include "State.h"
#include "ShaderCache.h"
#include "Util.h"
#include "RE/C/Calendar.h"
#include "Globals.h"

#include <cmath>

#include <d3d11.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes,
	ShowTriVisualizer,
	EnableTessellation,
	TessellationMinDistance,
	TessellationMaxDistance,
	TessellationMinFactor,
	TessellationMaxFactor,
	DetailHeightScale,
	WaveIntensity,
	WaveAmplitude,
	WaveSpeed,
	WaveSteepness,
	WavePrimaryContribution,
	WaveSecondaryContribution,
	WaveDetailContribution,
	WavePrimarySpeed,
	WaveSecondarySpeed,
	WaveDetailSpeed,
	WaveDirectionBlend,
	DisableVanillaWaterFoam,
	EnableLightingOverrides,
	FresnelBias,
	FresnelPower,
	ReflectionStrength,
	RefractionStrength,
	WaterTransparency,
	AbsorptionDensity,
	ScatteringCoeff,
	SpecularIntensity,
	SunSpecularPower,
	SunSpecularMagnitude,
	SunSparklePower,
	SunSparkleMagnitude,
	SpecularRadius,
	SpecularBrightness,
	AboveWaterFogDistNear,
	AboveWaterFogDistFar,
	AboveWaterFogAmount,
	UnderwaterFogDistNear,
	UnderwaterFogDistFar,
	UnderwaterFogAmount,
	DepthReflections,
	DepthRefractions,
	DepthNormals,
	DepthSpecularLighting)

void UnifiedWater::LoadSettings(json& o_json)
{
	settings = o_json;
}

void UnifiedWater::SaveSettings(json& o_json)
{
	o_json = settings;
}

void UnifiedWater::RestoreDefaultSettings()
{
	settings = {};
}

void UnifiedWater::DrawSettings()
{
	if (ImGui::BeginTabBar("UnifiedWaterTabs")) {
		if (ImGui::BeginTabItem("General")) {
			ImGui::Checkbox("Use Optimised Meshes", &settings.UseOptimisedMeshes);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Uses meshes with lower tri-count for improved performance.\nRequires location change or restart.");
			}

			ImGui::Checkbox("Enable Tessellation", &settings.EnableTessellation);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Hardware tessellation for dynamic mesh density based on distance.");
			}

			if (settings.EnableTessellation) {
				ImGui::Indent();
				ImGui::SliderFloat("Min Distance", &settings.TessellationMinDistance, 64.0f, 1024.0f, "%.0f");
				ImGui::SliderFloat("Max Distance", &settings.TessellationMaxDistance, 1024.0f, 16384.0f, "%.0f");
				ImGui::SliderFloat("Min Factor", &settings.TessellationMinFactor, 1.0f, 4.0f, "%.0f");
				ImGui::SliderFloat("Max Factor", &settings.TessellationMaxFactor, 4.0f, 64.0f, "%.0f");
				ImGui::SliderFloat("Detail Height Scale", &settings.DetailHeightScale, 0.0f, 10.0f, "%.2f");
				ImGui::Unindent();
			}

			ImGui::Spacing();
			ImGui::Checkbox("Disable Vanilla Water Foam", &settings.DisableVanillaWaterFoam);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Waves")) {
			ImGui::Text("Wave System");
			ImGui::SliderFloat("Wave Enhancement", &settings.WaveIntensity, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Wave Height", &settings.WaveAmplitude, 0.1f, 10.0f, "%.2f");
			ImGui::SliderFloat("Wave Speed", &settings.WaveSpeed, 0.01f, 1.0f, "%.3f");
			ImGui::SliderFloat("Wave Steepness", &settings.WaveSteepness, 0.1f, 10.0f, "%.2f");

			ImGui::Spacing();
			ImGui::Text("Wave Composition");
			ImGui::SliderFloat("Primary Contribution", &settings.WavePrimaryContribution, 0.0f, 1.5f, "%.2f");
			ImGui::SliderFloat("Secondary Contribution", &settings.WaveSecondaryContribution, 0.0f, 1.5f, "%.2f");
			ImGui::SliderFloat("Detail Contribution", &settings.WaveDetailContribution, 0.0f, 1.5f, "%.2f");
			ImGui::SliderFloat("Primary Speed Mult", &settings.WavePrimarySpeed, 0.0f, 2.0f, "%.2f");
			ImGui::SliderFloat("Secondary Speed Mult", &settings.WaveSecondarySpeed, 0.0f, 2.0f, "%.2f");
			ImGui::SliderFloat("Detail Speed Mult", &settings.WaveDetailSpeed, 0.0f, 3.0f, "%.2f");
			ImGui::SliderFloat("Direction Blend", &settings.WaveDirectionBlend, 0.0f, 3.0f, "%.2f");

			ImGui::Spacing();
			if (ImGui::TreeNodeEx("Wave 1 (Primary)", ImGuiTreeNodeFlags_None)) {
				ImGui::SliderFloat("W1 Amplitude", &settings.Wave1Amplitude, 0.0f, 6.0f, "%.2f");
				ImGui::SliderFloat("W1 Wavelength", &settings.Wave1Wavelength, 50.0f, 800.0f, "%.0f");
				ImGui::SliderFloat("W1 Steepness", &settings.Wave1Steepness, 0.0f, 0.6f, "%.3f");
				ImGui::SliderFloat("W1 Angle", &settings.Wave1AngleOffset, -180.0f, 180.0f, "%.1f");
				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx("Wave 2 (Secondary)", ImGuiTreeNodeFlags_None)) {
				ImGui::SliderFloat("W2 Amplitude", &settings.Wave2Amplitude, 0.0f, 4.0f, "%.2f");
				ImGui::SliderFloat("W2 Wavelength", &settings.Wave2Wavelength, 30.0f, 400.0f, "%.0f");
				ImGui::SliderFloat("W2 Steepness", &settings.Wave2Steepness, 0.0f, 0.5f, "%.3f");
				ImGui::SliderFloat("W2 Angle", &settings.Wave2AngleOffset, -180.0f, 180.0f, "%.1f");
				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx("Wave 3 (Detail)", ImGuiTreeNodeFlags_None)) {
				ImGui::SliderFloat("W3 Amplitude", &settings.Wave3Amplitude, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("W3 Wavelength", &settings.Wave3Wavelength, 15.0f, 200.0f, "%.0f");
				ImGui::SliderFloat("W3 Steepness", &settings.Wave3Steepness, 0.0f, 0.4f, "%.3f");
				ImGui::SliderFloat("W3 Angle", &settings.Wave3AngleOffset, -180.0f, 180.0f, "%.1f");
				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx("Fine Ripples (4-6)", ImGuiTreeNodeFlags_None)) {
				ImGui::SliderFloat("W4 Amplitude", &settings.Wave4Amplitude, 0.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("W4 Wavelength", &settings.Wave4Wavelength, 8.0f, 80.0f, "%.0f");
				ImGui::SliderFloat("W4 Steepness", &settings.Wave4Steepness, 0.0f, 0.3f, "%.3f");
				ImGui::SliderFloat("W4 Angle", &settings.Wave4AngleOffset, -180.0f, 180.0f, "%.1f");
				ImGui::Spacing();
				ImGui::SliderFloat("W5 Amplitude", &settings.Wave5Amplitude, 0.0f, 0.5f, "%.2f");
				ImGui::SliderFloat("W5 Wavelength", &settings.Wave5Wavelength, 4.0f, 40.0f, "%.0f");
				ImGui::SliderFloat("W5 Steepness", &settings.Wave5Steepness, 0.0f, 0.25f, "%.3f");
				ImGui::SliderFloat("W5 Angle", &settings.Wave5AngleOffset, -180.0f, 180.0f, "%.1f");
				ImGui::Spacing();
				ImGui::SliderFloat("W6 Amplitude", &settings.Wave6Amplitude, 0.0f, 0.3f, "%.2f");
				ImGui::SliderFloat("W6 Wavelength", &settings.Wave6Wavelength, 2.0f, 20.0f, "%.0f");
				ImGui::SliderFloat("W6 Steepness", &settings.Wave6Steepness, 0.0f, 0.2f, "%.3f");
				ImGui::SliderFloat("W6 Angle", &settings.Wave6AngleOffset, -180.0f, 180.0f, "%.1f");
				ImGui::TreePop();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Lighting")) {
			ImGui::Checkbox("Enable Lighting Overrides", &settings.EnableLightingOverrides);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Override ESP water lighting values with custom settings.");
			}

			if (settings.EnableLightingOverrides) {
				ImGui::Spacing();
				ImGui::Text("Fresnel / Reflection");
				ImGui::SliderFloat("Fresnel Bias (F0)", &settings.FresnelBias, 0.0f, 0.2f, "%.3f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Base reflectivity. Water IOR 1.33 = ~0.02.");
				}
				ImGui::SliderFloat("Fresnel Power", &settings.FresnelPower, 1.0f, 10.0f, "%.1f");
				ImGui::SliderFloat("Reflection Strength", &settings.ReflectionStrength, 0.0f, 2.0f, "%.2f");

				ImGui::Spacing();
				ImGui::Text("Refraction / Transparency");
				ImGui::SliderFloat("Refraction Strength", &settings.RefractionStrength, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Water Transparency", &settings.WaterTransparency, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Absorption Density", &settings.AbsorptionDensity, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Scattering", &settings.ScatteringCoeff, 0.0f, 0.5f, "%.3f");
				ImGui::SliderFloat("Specular Intensity", &settings.SpecularIntensity, 0.0f, 5.0f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Overall multiplier for all specular effects.");
				}

				ImGui::Spacing();
				ImGui::Text("Sun Specular");
				ImGui::SliderFloat("Sun Specular Power", &settings.SunSpecularPower, 10.0f, 1000.0f, "%.0f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Sharpness of sun reflection. Higher = tighter highlight.");
				}
				ImGui::SliderFloat("Sun Specular Magnitude", &settings.SunSpecularMagnitude, 0.0f, 5.0f, "%.2f");
				ImGui::SliderFloat("Sun Sparkle Power", &settings.SunSparklePower, 1.0f, 200.0f, "%.0f");
				ImGui::SliderFloat("Sun Sparkle Magnitude", &settings.SunSparkleMagnitude, 0.0f, 5.0f, "%.2f");
				ImGui::SliderFloat("Specular Radius", &settings.SpecularRadius, 1.0f, 512.0f, "%.0f");
				ImGui::SliderFloat("Specular Brightness", &settings.SpecularBrightness, 0.0f, 5.0f, "%.2f");

				ImGui::Spacing();
				ImGui::Text("Depth Control");
				ImGui::SliderFloat("Depth Reflections", &settings.DepthReflections, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Depth Refractions", &settings.DepthRefractions, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Depth Normals", &settings.DepthNormals, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Depth Specular", &settings.DepthSpecularLighting, 0.0f, 2.0f, "%.2f");
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Fog")) {
			if (!settings.EnableLightingOverrides) {
				ImGui::TextDisabled("Enable Lighting Overrides to use fog settings.");
			} else {
				ImGui::Text("Above Water Fog");
				ImGui::SliderFloat("Near Distance", &settings.AboveWaterFogDistNear, 0.0f, 10000.0f, "%.0f");
				ImGui::SliderFloat("Far Distance", &settings.AboveWaterFogDistFar, 1000.0f, 500000.0f, "%.0f");
				ImGui::SliderFloat("Fog Amount", &settings.AboveWaterFogAmount, 0.0f, 2.0f, "%.2f");

				ImGui::Spacing();
				ImGui::Text("Underwater Fog");
				ImGui::SliderFloat("UW Near Distance", &settings.UnderwaterFogDistNear, 0.0f, 1000.0f, "%.0f");
				ImGui::SliderFloat("UW Far Distance", &settings.UnderwaterFogDistFar, 100.0f, 20000.0f, "%.0f");
				ImGui::SliderFloat("UW Fog Amount", &settings.UnderwaterFogAmount, 0.0f, 2.0f, "%.2f");
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Debug")) {
			ImGui::Checkbox("Show Tri Visualizer", &settings.ShowTriVisualizer);

			if (ImGui::Button("Regenerate Flowmap") && flowmap) {
				if (flowmap->RegenerateAndLoadFlowmap(waterCache))
					SetFlowmapTex();
			}

			if (ImGui::Button("Regenerate Caches") && waterCache)
				waterCache->RegenerateCaches();

			if (ImGui::Button("Quick Test - Guardian Stones")) {
				if (auto ui = RE::UI::GetSingleton(); ui && !ui->menuStack.empty() && RE::PlayerCharacter::GetSingleton()) {
					RE::Console::ExecuteCommand("player.setav speedmult 1000");
					RE::Console::ExecuteCommand("tgm");
					RE::Console::ExecuteCommand("tcl");
					RE::Console::ExecuteCommand("set timescale to 0");
					RE::Console::ExecuteCommand("set gamehour to 12");
					RE::Console::ExecuteCommand("coc guardianstones");
					RE::Console::ExecuteCommand("fw 81a");
				}
			}

			if (ImGui::Button("Quick Test - Solitude Exterior")) {
				if (auto ui = RE::UI::GetSingleton(); ui && !ui->menuStack.empty() && RE::PlayerCharacter::GetSingleton()) {
					RE::Console::ExecuteCommand("player.setav speedmult 1000");
					RE::Console::ExecuteCommand("tgm");
					RE::Console::ExecuteCommand("tcl");
					RE::Console::ExecuteCommand("set timescale to 0");
					RE::Console::ExecuteCommand("set gamehour to 12");
					RE::Console::ExecuteCommand("coc solitudeexterior01");
					RE::Console::ExecuteCommand("fw 81a");
				}
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void UnifiedWater::DrawOverlay()
{
	if (!waterCache || !waterCache->IsBuildRunning() && !waterCache->HasBuildFailed())
		return;

	const auto shaderCache = globals::shaderCache;
	const float vOffset = shaderCache->IsCompiling() || shaderCache->GetFailedTasks() > 0 && !shaderCache->IsHideErrors() ? 120.0f : 0.0f;

	const auto snapshot = waterCache->GetBuildProgressSnapshot();

	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	if (waterCache->IsBuildRunning()) {
		auto progressTitle = fmt::format("Generating Water Cache:");
		auto percent = static_cast<float>(snapshot.completed) / static_cast<float>(snapshot.total);
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", snapshot.completed, snapshot.total, 100 * percent);

		ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());

		ImGui::End();
	} else if (waterCache->HasBuildFailed()) {
		ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}

		ImGui::TextColored(themeSettings.StatusPalette.Error, "ERROR: Water cache generation failed for %d WorldSpaces. Check installation and CommunityShaders.log", snapshot.failed);

		ImGui::End();
	}
}

bool UnifiedWater::IsOverlayVisible() const
{
	return true;
}

void UnifiedWater::DataLoaded()
{
	auto args = RE::BSModelDB::DBTraits::ArgsType();
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	RE::NiPointer<RE::NiNode> nif;

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\watermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load water mesh");
		return;
	}
	// TODO error check this properly
	const auto waterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	waterMesh = RE::NiPointer(waterShape);
	logger::debug("[Unified Water] Water mesh loaded");
	if (waterMesh) {
		auto& baseRuntime = waterMesh->GetTrishapeRuntimeData();
		baseVertexCount = baseRuntime.vertexCount;
		baseTriangleCount = baseRuntime.triangleCount;
	}

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\optimisedwatermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load optimised water mesh");
		return;
	}
	// TODO error check this properly
	const auto optimisedWaterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	optimisedWaterMesh = RE::NiPointer(optimisedWaterShape);
	logger::debug("[Unified Water] Optimised water mesh loaded");
	if (optimisedWaterMesh) {
		auto& optRuntime = optimisedWaterMesh->GetTrishapeRuntimeData();
		optimisedVertexCount = optRuntime.vertexCount;
		optimisedTriangleCount = optRuntime.triangleCount;
	}

	flowmap = new Flowmap();
	waterCache = new WaterCache();

	const bool rebuildAssets = LoadOrderChanged();
	if (rebuildAssets) {
		logger::info("[Unified Water] Load order changed, regenerating caches and dependent textures");
		waterCache->RegenerateCaches();
	} else {
		waterCache->LoadOrGenerateCaches();
	}

	while (waterCache->IsBuildRunning()) {
		std::this_thread::sleep_for(100ms);
	}

	if (waterCache->HasBuildFailed()) {
		logger::error("[Unified Water] Water cache build failed - systems will be degraded");
	}

	const bool flowmapReady = rebuildAssets ? flowmap->RegenerateAndLoadFlowmap(waterCache) : flowmap->LoadOrGenerateFlowmap(waterCache);
	if (flowmapReady)
		SetFlowmapTex();
}

bool UnifiedWater::LoadOrderChanged()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return false;

	uint64_t hash = 14695981039346656037ull;

	auto addToHash = [&](const RE::TESFile* file) {
		if (!file || !file->fileName)
			return;
		for (auto p = reinterpret_cast<const unsigned char*>(file->fileName); *p; ++p) {
			hash ^= *p;
			hash *= 1099511628211ull;
		}
	};

	if (const auto mods = dataHandler->GetLoadedMods()) {
		const uint32_t count = dataHandler->GetLoadedModCount();
		for (uint32_t i = 0, n = count; i < n; ++i)
			addToHash(mods[i]);
	}

	if (const auto lightMods = dataHandler->GetLoadedLightMods()) {
		const uint32_t count = dataHandler->GetLoadedLightModCount();
		for (uint32_t i = 0, n = count; i < n; ++i)
			addToHash(lightMods[i]);
	}

	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetDataPath() / "UWLoadOrder.hash";

	uint64_t existingHash = 0;
	if (fs::exists(path)) {
		std::ifstream file(path, std::ios::binary);
		if (file.is_open()) {
			file.read(reinterpret_cast<char*>(&existingHash), sizeof(existingHash));
			file.close();
		}
	}

	if (hash != existingHash) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (file.is_open()) {
			file.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
		}
	}

	return hash != existingHash;
}

void UnifiedWater::SetFlowmapTex() const
{
	RE::NiPointer<RE::NiSourceTexture> tex;
	if (!flowmap->TryGetFlowmap(tex))
		return;

	*gFlowMapSourceTex = tex;
	*gFlowMapSize = flowmap->GetWidth();

	logger::debug("[Unified Water] [Flowmap] Texture set");
}

void UnifiedWater::SetupResources()
{
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>());
	perTile = new ConstantBuffer(ConstantBufferDesc<PerTile>());
	tessellationParams = new ConstantBuffer(ConstantBufferDesc<TessellationParams>());

	// Compile tessellation shaders
	// Using SPECULAR + FLOWMAP + BLEND_NORMALS as the common water permutation
	// NUM_SPECULAR_LIGHTS must match what the game's VS uses for correct VS_OUTPUT structure
	std::vector<std::pair<const char*, const char*>> tessDefines = {
		{ "HSHADER", "" },
		{ "UNIFIED_WATER", "" },
		{ "SPECULAR", "" },
		{ "NUM_SPECULAR_LIGHTS", "0" },
		{ "FLOWMAP", "" },
		{ "BLEND_NORMALS", "" },
		{ "NORMAL_TEXCOORD", "" }
	};

	if (auto* hullShader = static_cast<ID3D11HullShader*>(Util::CompileShader(L"Data\\Shaders\\Water.hlsl", tessDefines, "hs_5_0"))) {
		waterHullShader.attach(hullShader);
	} else {
		logger::error("[Unified Water] Failed to compile hull shader");
	}

	// Domain shader with same defines but DSHADER instead of HSHADER
	tessDefines[0] = { "DSHADER", "" };

	if (auto* domainShader = static_cast<ID3D11DomainShader*>(Util::CompileShader(L"Data\\Shaders\\Water.hlsl", tessDefines, "ds_5_0"))) {
		waterDomainShader.attach(domainShader);
	} else {
		logger::error("[Unified Water] Failed to compile domain shader");
	}

	// Geometry shader for proper per-triangle barycentric coordinates (needed for tri visualizer)
	tessDefines[0] = { "GSHADER", "" };

	if (auto* geometryShader = static_cast<ID3D11GeometryShader*>(Util::CompileShader(L"Data\\Shaders\\Water.hlsl", tessDefines, "gs_5_0"))) {
		waterGeometryShader.attach(geometryShader);
	} else {
		logger::error("[Unified Water] Failed to compile geometry shader");
	}
}

void UnifiedWater::Reset()
{
	// Update the constant buffer when settings change
	hasLastTimingSample = false;
	lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	lastGameTimeHours = 0.0f;
	lastRealTimeSeconds = 0.0f;
	lastTimeScale = 1.0f;
	currentGameTimeHours = 0.0f;
	currentRealTimeSeconds = 0.0f;
	currentTimeScale = 1.0f;
	prevTileData.clear();
}

void UnifiedWater::PostPostLoad()
{
	stl::detour_thunk<TES_SetWorldSpace>(REL::RelocationID(13170, 13315));
	stl::detour_thunk<TES_DestroySkyCell>(REL::RelocationID(20029, 20463));

	stl::detour_thunk<TESWaterSystem_InitializeWater>(REL::RelocationID(31388, 32179));
	stl::write_thunk_call<TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams>(REL::RelocationID(31388, 32179).address() + REL::Relocate(0x360, 0x3BC, 0x35B));
	stl::write_vfunc<0x4, BSWaterShaderMaterial_ComputeCRC32>(RE::VTABLE_BSWaterShaderMaterial[0]);

	stl::detour_thunk<BGSTerrainBlock_Attach>(REL::RelocationID(30934, 31737));
	// Skip iterating attached meshes and calling TESWaterSystem::AddLODWater, this is handled in Attach now
	const auto addLoopOffset = REL::RelocationID(30934, 31737).address() + REL::Relocate(0x109, 0x109);
	if (REL::Module::IsAE())
		REL::safe_write(addLoopOffset, &REL::JMP8, 1);
	else {
		constexpr std::uint8_t patch[2] = { REL::NOP, REL::JMP32 };
		REL::safe_write(addLoopOffset, patch, 2);
	}

	stl::detour_thunk<BGSTerrainBlock_Detach>(REL::RelocationID(30936, 31739));

	stl::detour_thunk<BGSTerrainNode_UpdateWaterMeshSubVisibility>(REL::RelocationID(31059, 31846));

	stl::detour_thunk<TESWaterSystem_UpdateDisplacementMeshPosition>(REL::RelocationID(31384, 32175));

	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);
	stl::write_vfunc<0x7, BSWaterShader_RestoreGeometry>(RE::VTABLE_BSWaterShader[0]);

	// Patch out the code compute shader calls that write to the flow map in Main::RenderWaterEffects
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1B7, 0x1F7), REL::NOP, 5);
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1EA, 0x22A), REL::NOP, 5);
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x202, 0x242), REL::NOP, 5);

	gWaterLOD = reinterpret_cast<RE::NiNode**>(REL::RelocationID(516171, 402322).address());
	gFlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
	gFlowMapSourceTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(527694, 414616).address());
	gDisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
	gDisplacementMeshPos = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(516235, 402400).address());
	gDisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());

	logger::info("[Unified Water] Installed hooks");
}

void UnifiedWater::TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams::thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material)
{
	// The game prefills the material and hashes its contents, it uses this hash to check if there is an existing identical material and swaps
	// to using that material if so.
	// Problem is it does not include all data from the form, especially normal textures which can cause problems with existing materials
	// having their textures swapped out.
	// This func hash the texture names and temporarily stashes them in a ptr slot, this is added to the hash in ComputeCRC and zeroed back out again
	func(form, material);

	uint32_t hash = 2166136261u;
	auto addStrToHash = [&](const char* str) {
		for (auto p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
			hash ^= *p;
			hash *= 16777619u;
		}
	};

	addStrToHash(form->noiseTextures[0].textureName.c_str());
	addStrToHash(form->noiseTextures[1].textureName.c_str());
	addStrToHash(form->noiseTextures[2].textureName.c_str());
	addStrToHash(form->noiseTextures[3].textureName.c_str());
	uintptr_t bits = hash;
	std::memcpy(&material->normalTexture1, &bits, sizeof(uintptr_t));
}

void UnifiedWater::TESWaterSystem_InitializeWater::thunk(RE::TESWaterSystem* waterSystem, RE::BSTriShape* waterTri, RE::TESWaterForm* form, float waterHeight, void* unk4, bool noDisplacement, bool isProcedural)
{
	func(waterSystem, waterTri, form, waterHeight, unk4, noDisplacement, isProcedural);
}

int32_t UnifiedWater::BSWaterShaderMaterial_ComputeCRC32::thunk(RE::BSWaterShaderMaterial* material, uint32_t srcHash)
{
	srcHash ^= static_cast<uint32_t>(reinterpret_cast<uint64_t>(material->normalTexture1.get())) + (srcHash << 6) + (srcHash >> 2);
	constexpr auto zero = static_cast<uintptr_t>(0);
	std::memcpy(&material->normalTexture1, &zero, sizeof(uintptr_t));
	return func(material, srcHash);
}

void UnifiedWater::TES_SetWorldSpace::thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior)
{
	func(tes, worldSpace, isExterior);

	auto& singleton = globals::features::unifiedWater;
	singleton.prevTileData.clear();
	singleton.hasLastTimingSample = false;
	singleton.lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	singleton.lastGameTimeHours = 0.0f;
	singleton.lastRealTimeSeconds = 0.0f;
	singleton.lastTimeScale = 1.0f;
	singleton.currentGameTimeHours = 0.0f;
	singleton.currentRealTimeSeconds = 0.0f;
	singleton.currentTimeScale = 1.0f;
	singleton.waterCache->SetCurrentWorldSpace(worldSpace);
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	auto& singleton = globals::features::unifiedWater;
	singleton.prevTileData.clear();
	singleton.hasLastTimingSample = false;
	singleton.lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	singleton.lastGameTimeHours = 0.0f;
	singleton.lastRealTimeSeconds = 0.0f;
	singleton.lastTimeScale = 1.0f;
	singleton.currentGameTimeHours = 0.0f;
	singleton.currentRealTimeSeconds = 0.0f;
	singleton.currentTimeScale = 1.0f;
	singleton.waterCache->SetCurrentWorldSpace(nullptr);
}

void UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent)
{
	if (!node || !waterParent)
		return;

	if (node->GetLODLevel() != 4)
		return;

	const auto tes = globals::game::tes;
	if (!tes || !tes->gridCells)
		return;
	
	const auto& gridCells = tes->gridCells;

	const int32_t offsetX = tes->currentGridX - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t offsetY = tes->currentGridY - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t length = static_cast<int32_t>(gridCells->length);

	for (const auto& child : waterParent->GetChildren()) {
		if (!child)
			continue;

		int32_t x, y;
		Util::WorldToCell(child->world.translate, x, y);

		x -= offsetX;
		y -= offsetY;

		bool cull = false;
		if (x >= 0 && y >= 0 && x < length && y < length) {
			if (const auto cell = gridCells->GetCell(x, y); cell && cell->cellState.any(RE::TESObjectCELL::CellState::kAttached, static_cast<RE::TESObjectCELL::CellState>(6)))
				cull = true;
		}

		child->SetAppCulled(cull);
	}
}

void UnifiedWater::BGSTerrainBlock_Attach::thunk(RE::BGSTerrainBlock* block)
{
	const auto waterSystem = RE::TESWaterSystem::GetSingleton();
	if (!waterSystem) {
		return;
	}

	auto& singleton = globals::features::unifiedWater;

	std::vector<std::pair<RE::BSTriShape*, const WaterCache::Instruction*>> built;
	bool attaching = false;

	if (block && block->loaded && !block->attached && block->chunk && block->water) {
		block->chunk->DetachChild2(block->water);
		block->water->local.translate = block->chunk->local.translate;

		RE::NiUpdateData updateData;
		block->water->UpdateUpwardPass(updateData);

		const auto water = block->water;
		for (auto& child : water->GetChildren()) {
			if (child) {
				waterSystem->RemoveGeometry(child->AsGeometry());
				water->DetachChild(child.get());
			}
		}

		attaching = true;

		const auto node = block->node;
		const auto lodLevel = node->GetLODLevel();
		const auto worldSpace = block->node->manager->worldSpace;

		const auto instructions = singleton.waterCache->GetInstructions(worldSpace, lodLevel, node->x, node->y);
		if (!instructions) {
			logger::warn("[Unified Water] No instructions found for {} chunk at {}, {}", worldSpace->GetFormEditorID(), node->x, node->y);
			func(block);
			return;
		}

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const bool farLOD = lodLevel > 8;
			
			bool useOptimised = singleton.settings.UseOptimisedMeshes;
			if (farLOD)
				useOptimised = false;  // Always keep far LOD water at normal vertex counts

			RE::BSTriShape* templateShape = nullptr;
			if (useOptimised) {
				templateShape = singleton.optimisedWaterMesh.get();
			}

			if (!templateShape) {
				templateShape = singleton.waterMesh.get();
			}

			if (!templateShape)
				continue;

			RE::BSTriShape* shape = templateShape->CreateClone(cloningProcess)->AsTriShape();

			const auto posX = (instruction.x - node->x) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->y) * 4096.0f + instruction.size * 2048.0f;
			shape->local.scale = static_cast<float>(instruction.size);
			shape->local.translate = { posX, posY, instruction.waterHeight };
			
			// Store LOD level in the shape name for later retrieval during rendering
			// Format: "WaterLOD_<level>" (e.g., "WaterLOD_8")
			char nameBuf[32];
			sprintf_s(nameBuf, "WaterLOD_%d", lodLevel);
			shape->name = nameBuf;

			water->AttachChild(shape, true);
			built.emplace_back(shape, &instruction);

			block->waterAttached = true;
		}
	}

	func(block);

	if (!attaching || !block->waterAttached)
		return;

	for (auto& [shape, instruction] : built) {
		waterSystem->InitializeWater(shape, instruction->form.ptr, instruction->waterHeight, nullptr, false, false);

		if (const auto prop = shape->GetGeometryRuntimeData().properties[1].get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			REX::EnumSet waterFlags = static_cast<RE::BSWaterShaderProperty::WaterFlag>(0b10000100);
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseCubemapReflections;
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseReflections;
			if (instruction->form.ptr->flags.any(RE::TESWaterForm::Flag::kEnableFlowmap))
				waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kEnableFlowmap;
			if (instruction->form.ptr->flags.any(RE::TESWaterForm::Flag::kBlendNormals))
				waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kBlendNormals;
			waterShaderProp->waterFlags = waterFlags;
		}

		// Remove from WaterSystem, will manage it ourselves
		waterSystem->waterObjects.pop_back();
	}

	(*singleton.gWaterLOD)->AttachChild(block->water, true);
	waterSystem->Enable();
}

void UnifiedWater::BGSTerrainBlock_Detach::thunk(RE::BGSTerrainBlock* block)
{
	const auto water = block->water;
	block->water = nullptr;

	func(block);

	block->water = water;

	if (water) {
		auto count = water->GetChildren().size();
		while (count > 0) {
			water->DetachChildAt(--count);
		}

		(*globals::features::unifiedWater.gWaterLOD)->DetachChild(water);
		block->waterAttached = false;
	}
}

void UnifiedWater::BSWaterShader_SetupGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass)
{
	auto& singleton = globals::features::unifiedWater;

	// Update and bind the per-frame constant buffer for vertex shader access
	if (singleton.perFrame) {
		PerFrame perFrameData{};
		perFrameData.WaveIntensity = singleton.settings.WaveIntensity;
		perFrameData.WaveAmplitude = singleton.settings.WaveAmplitude;
		perFrameData.WaveSpeed = singleton.settings.WaveSpeed;
		perFrameData.WaveSteepness = singleton.settings.WaveSteepness;
		
		// Water lighting override parameters
		perFrameData.EnableLightingOverrides = singleton.settings.EnableLightingOverrides ? 1.0f : 0.0f;
		perFrameData.FresnelBias = singleton.settings.FresnelBias;
		perFrameData.FresnelPower = singleton.settings.FresnelPower;
		perFrameData.ReflectionStrength = singleton.settings.ReflectionStrength;
		perFrameData.RefractionStrength = singleton.settings.RefractionStrength;
		perFrameData.WaterTransparency = singleton.settings.WaterTransparency;
		perFrameData.AbsorptionDensity = singleton.settings.AbsorptionDensity;
		perFrameData.ScatteringCoeff = singleton.settings.ScatteringCoeff;
		perFrameData.SpecularIntensity = singleton.settings.SpecularIntensity;
		
		// Sun specular overrides
		perFrameData.SunSpecularPower = singleton.settings.SunSpecularPower;
		perFrameData.SunSpecularMagnitude = singleton.settings.SunSpecularMagnitude;
		perFrameData.SunSparklePower = singleton.settings.SunSparklePower;
		perFrameData.SunSparkleMagnitude = singleton.settings.SunSparkleMagnitude;
		perFrameData.SpecularRadius = singleton.settings.SpecularRadius;
		perFrameData.SpecularBrightness = singleton.settings.SpecularBrightness;
		
		// Fog overrides
		perFrameData.AboveWaterFogDistNear = singleton.settings.AboveWaterFogDistNear;
		perFrameData.AboveWaterFogDistFar = singleton.settings.AboveWaterFogDistFar;
		perFrameData.AboveWaterFogAmount = singleton.settings.AboveWaterFogAmount;
		perFrameData.UnderwaterFogDistNear = singleton.settings.UnderwaterFogDistNear;
		perFrameData.UnderwaterFogDistFar = singleton.settings.UnderwaterFogDistFar;
		perFrameData.UnderwaterFogAmount = singleton.settings.UnderwaterFogAmount;
		
		// Depth properties
		perFrameData.DepthReflections = singleton.settings.DepthReflections;
		perFrameData.DepthRefractions = singleton.settings.DepthRefractions;
		perFrameData.DepthNormals = singleton.settings.DepthNormals;
		perFrameData.DepthSpecularLighting = singleton.settings.DepthSpecularLighting;
		
		perFrameData.WavePrimaryContribution = singleton.settings.WavePrimaryContribution;
		perFrameData.WaveSecondaryContribution = singleton.settings.WaveSecondaryContribution;
		perFrameData.WaveDetailContribution = singleton.settings.WaveDetailContribution;
		perFrameData.WavePrimarySpeed = singleton.settings.WavePrimarySpeed;
		perFrameData.WaveSecondarySpeed = singleton.settings.WaveSecondarySpeed;
		perFrameData.WaveDetailSpeed = singleton.settings.WaveDetailSpeed;
		perFrameData.WaveDirectionBlend = singleton.settings.WaveDirectionBlend;
		perFrameData.TriVisualizerEnabled = singleton.settings.ShowTriVisualizer ? 1.0f : 0.0f;
		
		// Wave parameters (Period removed - speed now calculated from wavelength via physics)
		perFrameData.Wave1Amplitude = singleton.settings.Wave1Amplitude;
		perFrameData.Wave1Wavelength = singleton.settings.Wave1Wavelength;
		perFrameData.Wave1Steepness = singleton.settings.Wave1Steepness;
		
		perFrameData.Wave2Amplitude = singleton.settings.Wave2Amplitude;
		perFrameData.Wave2Wavelength = singleton.settings.Wave2Wavelength;
		perFrameData.Wave2Steepness = singleton.settings.Wave2Steepness;
		
		perFrameData.Wave3Amplitude = singleton.settings.Wave3Amplitude;
		perFrameData.Wave3Wavelength = singleton.settings.Wave3Wavelength;
		perFrameData.Wave3Steepness = singleton.settings.Wave3Steepness;
		
		perFrameData.Wave4Amplitude = singleton.settings.Wave4Amplitude;
		perFrameData.Wave4Wavelength = singleton.settings.Wave4Wavelength;
		perFrameData.Wave4Steepness = singleton.settings.Wave4Steepness;
		
		perFrameData.Wave5Amplitude = singleton.settings.Wave5Amplitude;
		perFrameData.Wave5Wavelength = singleton.settings.Wave5Wavelength;
		perFrameData.Wave5Steepness = singleton.settings.Wave5Steepness;
		
		perFrameData.Wave6Amplitude = singleton.settings.Wave6Amplitude;
		perFrameData.Wave6Wavelength = singleton.settings.Wave6Wavelength;
		perFrameData.Wave6Steepness = singleton.settings.Wave6Steepness;
		
		// Convert angles from degrees to radians
		perFrameData.Wave1AngleOffset = singleton.settings.Wave1AngleOffset * 0.0174532925f;  // degrees to radians
		perFrameData.Wave2AngleOffset = singleton.settings.Wave2AngleOffset * 0.0174532925f;
		perFrameData.Wave3AngleOffset = singleton.settings.Wave3AngleOffset * 0.0174532925f;
		perFrameData.Wave4AngleOffset = singleton.settings.Wave4AngleOffset * 0.0174532925f;
		perFrameData.Wave5AngleOffset = singleton.settings.Wave5AngleOffset * 0.0174532925f;
		perFrameData.Wave6AngleOffset = singleton.settings.Wave6AngleOffset * 0.0174532925f;

		// Set tessellation enabled flag - tells VS to skip wave displacement so DS can handle it
		bool tessellationEnabled = singleton.settings.EnableTessellation && 
		                           singleton.waterHullShader && 
		                           singleton.waterDomainShader &&
		                           singleton.waterGeometryShader;
		perFrameData.TessellationEnabled = tessellationEnabled ? 1.0f : 0.0f;
		perFrameData.TessPadding1 = 0.0f;
		perFrameData.TessPadding2 = 0.0f;
		perFrameData.TessPadding3 = 0.0f;

		const auto* state = globals::state;
		const std::uint32_t frameIndex = state ? state->frameCount : singleton.lastTimingFrameIndex;
		if (singleton.lastTimingFrameIndex != frameIndex) {
			if (singleton.hasLastTimingSample) {
				singleton.lastGameTimeHours = singleton.currentGameTimeHours;
				singleton.lastRealTimeSeconds = singleton.currentRealTimeSeconds;
				singleton.lastTimeScale = singleton.currentTimeScale;
			}
			singleton.lastTimingFrameIndex = frameIndex;
		}

		float gameTimeHours = 0.0f;
		float realTimeSeconds = 0.0f;
		float timeScale = 1.0f;

		if (const auto calendar = RE::Calendar::GetSingleton()) {
			gameTimeHours = calendar->GetHoursPassed();
			timeScale = calendar->GetTimescale();
		}

		if (globals::state) {
			realTimeSeconds = globals::state->timer;
		}

		perFrameData.GameTimeHours = gameTimeHours;
		perFrameData.RealTimeSeconds = realTimeSeconds;
		perFrameData.TimeScale = timeScale;
		perFrameData.CellWorldSize = 4096.0f;
		perFrameData.PrevGameTimeHours = singleton.hasLastTimingSample ? singleton.lastGameTimeHours : gameTimeHours;
		perFrameData.PrevRealTimeSeconds = singleton.hasLastTimingSample ? singleton.lastRealTimeSeconds : realTimeSeconds;
		perFrameData.PrevTimeScale = singleton.hasLastTimingSample ? singleton.lastTimeScale : timeScale;
		
		singleton.perFrame->Update(perFrameData);
		
		auto context = globals::d3d::context;
		ID3D11Buffer* buffers[1] = { singleton.perFrame->CB() };
		context->VSSetConstantBuffers(7, 1, buffers);
		context->PSSetConstantBuffers(7, 1, buffers); // Bind to pixel shader too for foam

		singleton.currentGameTimeHours = gameTimeHours;
		singleton.currentRealTimeSeconds = realTimeSeconds;
		singleton.currentTimeScale = timeScale;
		singleton.hasLastTimingSample = true;
	}

	// Get water tile position and LOD level for per-tile data
	int32_t x, y;
	Util::WorldToCell(pass->geometry->world.translate, x, y);

	// Determine LOD level from the shape name if available
	// LOD water shapes created by BGSTerrainBlock_Attach are named "WaterLOD_<level>"
	// Regular water cells managed by TESWaterSystem have no special name - use LOD1 for single-cell precision
	int32_t lodLevel = 1; // Default to LOD1 for regular water cells (single-cell resolution)

	if (pass->geometry->name.c_str()) {
		const char* name = pass->geometry->name.c_str();
		if (strncmp(name, "WaterLOD_", 9) == 0) {
			lodLevel = atoi(name + 9);
			// Validate it's a power of 2 in the expected range
			if (lodLevel != 1 && lodLevel != 4 && lodLevel != 8 && lodLevel != 16 && lodLevel != 32) {
				logger::warn("[Unified Water] Invalid LOD level {} parsed from name '{}', using LOD1", lodLevel, name);
				lodLevel = 1; // Fallback to LOD1 if invalid
			}
		}
	}

	// Update per-tile data for temporal blending
	if (singleton.perTile) {
		PerTile perTileData{};

		RE::TESWorldSpace* activeWorldSpace = nullptr;
		std::uint32_t worldSpaceId = 0;
		if (const auto tes = RE::TES::GetSingleton()) {
			activeWorldSpace = tes->GetRuntimeData2().worldSpace;
			if (activeWorldSpace) {
				worldSpaceId = activeWorldSpace->GetFormID();
			}
		}

		auto mixKey = [](std::uint64_t seed, std::uint64_t value) noexcept {
			seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
			return seed;
		};

		std::uint64_t tileKeySeed = 0;
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(worldSpaceId));
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(lodLevel)));
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)));
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)));
		const std::uint64_t tileKey = tileKeySeed;

		float prevNormalX = 0.0f;
		float prevNormalY = 0.0f;
		float prevDistance = 10000.0f;
		float prevSegments = 32.0f;
		const auto prevTileIt = singleton.prevTileData.find(tileKey);
		if (prevTileIt != singleton.prevTileData.end()) {
			prevNormalX = prevTileIt->second.normalX;
			prevNormalY = prevTileIt->second.normalY;
			prevDistance = prevTileIt->second.distance;
			prevSegments = prevTileIt->second.segmentsPerAxis;
		}

		perTileData.PrevData[0] = prevNormalX;
		perTileData.PrevData[1] = prevNormalY;
		perTileData.PrevData[2] = prevDistance;
		perTileData.PrevData[3] = prevSegments;

		perTileData.TileData[0] = static_cast<float>(x);
		perTileData.TileData[1] = static_cast<float>(y);
		perTileData.TileData[2] = static_cast<float>(lodLevel);
		perTileData.TileData[3] = 1.0f;

		float currentSegmentsPerAxis = prevSegments;
		if (const auto triShape = pass->geometry->AsTriShape()) {
			auto& runtimeData = triShape->GetTrishapeRuntimeData();
			const float triangleCount = static_cast<float>(runtimeData.triangleCount);
			if (triangleCount > 0.0f) {
				currentSegmentsPerAxis = std::max(1.0f, std::sqrt(triangleCount * 0.5f));
			}
		}
		perTileData.PrevData[3] = currentSegmentsPerAxis;

		float storedNormalX = prevNormalX;
		float storedNormalY = prevNormalY;
		float storedDistance = prevDistance;

		singleton.prevTileData[tileKey] = UnifiedWater::PrevTileData{ storedNormalX, storedNormalY, storedDistance, currentSegmentsPerAxis };

		singleton.perTile->Update(perTileData);

		auto context = globals::d3d::context;
		ID3D11Buffer* buffers[1] = { singleton.perTile->CB() };
		context->VSSetConstantBuffers(8, 1, buffers);
		context->PSSetConstantBuffers(8, 1, buffers); // Also bind to pixel shader for foam/normals
	}

	if (singleton.flowmap) {
		// ObjectUV.xyz below, xy contains width and height, z contains mesh scale
		// Previously flowmap size was in x, yz contained flowmap offset for water displacement mesh
		*singleton.gFlowMapSize = singleton.flowmap->GetWidth();                                            // ObjectUV.x
		singleton.gDisplacementMeshFlowCellOffset->x = static_cast<float>(singleton.flowmap->GetHeight());  // ObjectUV.y
		singleton.gDisplacementMeshFlowCellOffset->y = 1.0f - pass->geometry->local.scale;                  // ObjectUV.z (counters 1 - x in SetupGeometry)

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().properties[1].get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);

			// CellTexCoordOffset.xyzw below - applies to non-displacement water only
			// xy is world cell flowmap based (0,0 is corner of flow map), zw is world cell
			// Funky maths here to counter what's being done in SetupGeometry
			// Previously these values were relative to the 5x5 flow grid centered on the player
			waterShaderProp->flowX = x + singleton.flowmap->GetOffsetX();                                                                   // CellTexCoordOffset.x
			waterShaderProp->flowY = y + singleton.flowmap->GetOffsetY() + singleton.flowmap->GetWidth() - singleton.flowmap->GetHeight();  // CellTexCoordOffset.y
			waterShaderProp->cellX = x;                                                                                                     // CellTexCoordOffset.z
			waterShaderProp->cellY = y;                                                                                                     // CellTexCoordOffset.w
		}
	}

	// Extract technique from passEnum (bits 11-14 typically for water shader)
	uint32_t technique = (pass->passEnum >> 11) & 0xF;
	
	// Tessellation is only compatible with SPECULAR techniques (0-7)
	// UNDERWATER (8), LOD (9), STENCIL (10), SIMPLE (11) have different VS_OUTPUT structures
	// Our HS/DS are compiled with SPECULAR + FLOWMAP + BLEND_NORMALS defines
	bool techniqueSupportsTessel = (technique < 8);

	// Tessellation setup
	bool tessellationEnabled = singleton.settings.EnableTessellation && 
	                           singleton.waterHullShader && 
	                           singleton.waterDomainShader &&
	                           singleton.waterGeometryShader &&
	                           techniqueSupportsTessel;

	auto context = globals::d3d::context;

	// Clean up any lingering tessellation state from previous passes BEFORE calling func()
	// This ensures the original SetupGeometry sees a clean non-tessellated pipeline state
	if (tessellationActiveForPass) {
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		if (originalTopology != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED) {
			context->IASetPrimitiveTopology(originalTopology);
		}
	}

	tessellationActiveForPass = false;
	originalTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	static bool loggedTessSetup = false;
	static int tessFrameCount = 0;
	tessFrameCount++;
	bool shouldLog = !loggedTessSetup || (tessFrameCount % 1000 == 0);
	
	if (shouldLog) {
		logger::info("[Unified Water] SetupGeometry - passEnum:0x{:X} technique:{} numLights:{} tessCompat:{}", 
			pass->passEnum, technique, pass->numLights, techniqueSupportsTessel);
	}

	// CRITICAL: Call original SetupGeometry FIRST to set up VS, PS, textures, etc.
	// THEN apply tessellation state after, so it's not overwritten by the original
	func(waterShader, pass);

	if (tessellationEnabled) {
		if (shouldLog) {
			logger::info("[Unified Water] Tessellation enabled - HS: {:p}, DS: {:p}, GS: {:p}", 
				(void*)singleton.waterHullShader.get(), (void*)singleton.waterDomainShader.get(), (void*)singleton.waterGeometryShader.get());
		}

		// Update tessellation constant buffer with current camera position
		if (singleton.tessellationParams) {
			TessellationParams tessParams{};
			tessParams.TessellationMinDistance = singleton.settings.TessellationMinDistance;
			tessParams.TessellationMaxDistance = singleton.settings.TessellationMaxDistance;
			tessParams.TessellationMinFactor = singleton.settings.TessellationMinFactor;
			tessParams.TessellationMaxFactor = singleton.settings.TessellationMaxFactor;

			// Get camera world position using established utility function
			auto cameraPos = Util::GetEyePosition(0);
			tessParams.CameraWorldPosX = cameraPos.x;
			tessParams.CameraWorldPosY = cameraPos.y;
			tessParams.CameraWorldPosZ = cameraPos.z;
			tessParams.DetailHeightScale = singleton.settings.DetailHeightScale;
			
			if (shouldLog) {
				logger::info("[Unified Water] Tess params - MinDist:{} MaxDist:{} MinFactor:{} MaxFactor:{} CamPos:({},{},{}) DetailScale:{}",
					tessParams.TessellationMinDistance, tessParams.TessellationMaxDistance,
					tessParams.TessellationMinFactor, tessParams.TessellationMaxFactor,
					tessParams.CameraWorldPosX, tessParams.CameraWorldPosY, tessParams.CameraWorldPosZ,
					tessParams.DetailHeightScale);
			}

			singleton.tessellationParams->Update(tessParams);

			// Bind tessellation constant buffer to HS and DS
			ID3D11Buffer* tessBuffers[1] = { singleton.tessellationParams->CB() };
			context->HSSetConstantBuffers(9, 1, tessBuffers);
			context->DSSetConstantBuffers(9, 1, tessBuffers);
		}

		// Save original topology for RestoreGeometry (after original SetupGeometry has set it)
		context->IAGetPrimitiveTopology(&originalTopology);
		
		if (shouldLog) {
			logger::info("[Unified Water] Original topology after func: {}", static_cast<int>(originalTopology));
		}

		// Set patch list topology for tessellation (3 control points per patch)
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

		// Bind hull, domain, and geometry shaders
		context->HSSetShader(singleton.waterHullShader.get(), nullptr, 0);
		context->DSSetShader(singleton.waterDomainShader.get(), nullptr, 0);
		context->GSSetShader(singleton.waterGeometryShader.get(), nullptr, 0);
		
		// Verify shaders were bound and topology set
		if (shouldLog) {
			ID3D11HullShader* boundHS = nullptr;
			ID3D11DomainShader* boundDS = nullptr;
			ID3D11GeometryShader* boundGS = nullptr;
			context->HSGetShader(&boundHS, nullptr, nullptr);
			context->DSGetShader(&boundDS, nullptr, nullptr);
			context->GSGetShader(&boundGS, nullptr, nullptr);
			logger::info("[Unified Water] After bind - HS: {:p}, DS: {:p}, GS: {:p}", (void*)boundHS, (void*)boundDS, (void*)boundGS);
			if (boundHS) boundHS->Release();
			if (boundDS) boundDS->Release();
			if (boundGS) boundGS->Release();
			
			D3D11_PRIMITIVE_TOPOLOGY currentTopo;
			context->IAGetPrimitiveTopology(&currentTopo);
			logger::info("[Unified Water] After set - topology: {} (expected 35 for 3-control-point patch list)", static_cast<int>(currentTopo));
		}

		// Bind VS constant buffers to DS as well (DS needs the same transforms)
		// Do this AFTER func() so the original has set up the VS constant buffers
		ID3D11Buffer* vsBuffers[3] = { nullptr, nullptr, nullptr };
		context->VSGetConstantBuffers(0, 3, vsBuffers);
		context->DSSetConstantBuffers(0, 3, vsBuffers);
		
		// Bind the FrameBuffer cbuffer (b12) to DS - needed for CameraPosAdjust in wave calculations
		ID3D11Buffer* frameBuffer[1] = { nullptr };
		context->PSGetConstantBuffers(12, 1, frameBuffer);  // FrameBuffer is typically bound to PS
		if (frameBuffer[0]) {
			context->DSSetConstantBuffers(12, 1, frameBuffer);
		}
		
		if (shouldLog) {
			logger::info("[Unified Water] VS CBs bound to DS - b0:{:p} b1:{:p} b2:{:p} b12:{:p}", 
				(void*)vsBuffers[0], (void*)vsBuffers[1], (void*)vsBuffers[2], (void*)frameBuffer[0]);
		}

		// Also bind the UnifiedWater per-frame buffer to HS/DS
		if (singleton.perFrame) {
			ID3D11Buffer* perFrameBuffers[1] = { singleton.perFrame->CB() };
			context->HSSetConstantBuffers(7, 1, perFrameBuffers);
			context->DSSetConstantBuffers(7, 1, perFrameBuffers);
		}

		// Bind normal textures to DS for heightmap-based detail tessellation
		if (singleton.settings.DetailHeightScale > 0.0f) {
			ID3D11ShaderResourceView* normalSRVs[3] = { nullptr, nullptr, nullptr };
			ID3D11SamplerState* normalSamplers[3] = { nullptr, nullptr, nullptr };
			context->PSGetShaderResources(4, 3, normalSRVs);
			context->PSGetSamplers(4, 3, normalSamplers);
			context->DSSetShaderResources(4, 3, normalSRVs);
			context->DSSetSamplers(4, 3, normalSamplers);
			for (int i = 0; i < 3; i++) {
				if (normalSRVs[i]) normalSRVs[i]->Release();
				if (normalSamplers[i]) normalSamplers[i]->Release();
			}
		}

		tessellationActiveForPass = true;
		loggedTessSetup = true;
	} else if (!loggedTessSetup && singleton.settings.EnableTessellation) {
		logger::warn("[Unified Water] Tessellation enabled in settings but shaders missing - HS:{:p} DS:{:p} GS:{:p}",
			(void*)singleton.waterHullShader.get(), (void*)singleton.waterDomainShader.get(), (void*)singleton.waterGeometryShader.get());
		loggedTessSetup = true;
	}
}

void UnifiedWater::BSWaterShader_RestoreGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass, uint32_t renderFlags)
{
	// Restore tessellation state after the draw call
	if (tessellationActiveForPass) {
		auto context = globals::d3d::context;

		// Unbind hull, domain, and geometry shaders
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);

		// Restore original topology
		if (originalTopology != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED) {
			context->IASetPrimitiveTopology(originalTopology);
		}

		tessellationActiveForPass = false;
	}

	func(waterShader, pass, renderFlags);
}

void UnifiedWater::TESWaterSystem_UpdateDisplacementMeshPosition::thunk(RE::TESWaterSystem* waterSystem)
{
	func(waterSystem);

	const auto& singleton = globals::features::unifiedWater;
	if (!singleton.flowmap)
		return;

	const float posX = singleton.gDisplacementMeshPos->x / 4096.0f;
	const float posY = singleton.gDisplacementMeshPos->y / 4096.0f;
	const float offsetX = static_cast<float>(singleton.flowmap->GetOffsetX());
	const float offsetY = static_cast<float>(singleton.flowmap->GetOffsetY());
	const float height = static_cast<float>(singleton.flowmap->GetHeight());

	// CellTexCoordOffset.xyzw below - applies to displacement water only
	// Previously the values were calculated relative to the 5x5 flow grid
	*singleton.gDisplacementCellTexCoordOffset = float4(posX + offsetX, height - (posY + offsetY), posX, 1 - posY);
}
