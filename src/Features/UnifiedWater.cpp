#include "UnifiedWater.h"

#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "PCH.h"
#include "ShaderCache.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes,
	WaveIntensity,
	FoamIntensity)

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
	ImGui::Checkbox("Use Optimised Meshes", &settings.UseOptimisedMeshes);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Uses meshes with significantly lower tri-count for improved performance with no visual quality loss.\n"
			"Will only affect newly created water - requires a change of location or game restart to take effect.");
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Enhanced Water Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Enhanced Wave System");
		ImGui::SliderFloat("Wave Enhancement", &settings.WaveIntensity, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Controls Gerstner wave enhancement strength.\n"
				"Adds realistic directional waves for more dynamic water surface.\n"
				"Set to 0 to disable enhanced waves.");
		}

		ImGui::Spacing();

		ImGui::Text("Advanced Foam System");
		ImGui::SliderFloat("Foam Enhancement", &settings.FoamIntensity, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Controls depth-based foam generation strength.\n"
				"Provides more realistic foam patterns near shorelines.\n"
				"Set to 0 to disable enhanced foam.");
		}
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Regenerate Flowmap") && flowmap) {
			if (flowmap->RegenerateAndLoadFlowmap())
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

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\optimisedwatermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load optimised water mesh");
		return;
	}
	// TODO error check this properly
	const auto optimisedWaterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	optimisedWaterMesh = RE::NiPointer(optimisedWaterShape);
	logger::debug("[Unified Water] Optimised water mesh loaded");

	flowmap = new Flowmap();
	waterCache = new WaterCache();

	if (LoadOrderChanged()) {
		logger::info("[Unified Water] Load order changed, regenerating flowmap and caches");

		if (flowmap->RegenerateAndLoadFlowmap())
			SetFlowmapTex();

		waterCache->RegenerateCaches();
	} else {
		if (flowmap->LoadOrGenerateFlowmap())
			SetFlowmapTex();

		waterCache->LoadOrGenerateCaches();
	}

	while (waterCache->IsBuildRunning()) {
		std::this_thread::sleep_for(100ms);
	}
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

void UnifiedWater::PostPostLoad()
{
	stl::detour_thunk<TES_SetWorldSpace>(REL::RelocationID(13170, 13315));
	stl::detour_thunk<TES_DestroySkyCell>(REL::RelocationID(20029, 20463));

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

void UnifiedWater::TES_SetWorldSpace::thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior)
{
	func(tes, worldSpace, isExterior);

	globals::features::unifiedWater.waterCache->SetCurrentWorldSpace(worldSpace);
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	globals::features::unifiedWater.waterCache->SetCurrentWorldSpace(nullptr);
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

	const auto& singleton = globals::features::unifiedWater;

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

			const auto targetShape = lodLevel > 4 || singleton.settings.UseOptimisedMeshes ? singleton.optimisedWaterMesh : singleton.waterMesh;
			RE::BSTriShape* shape = targetShape->CreateClone(cloningProcess)->AsTriShape();

			const auto posX = (instruction.x - node->x) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->y) * 4096.0f + instruction.size * 2048.0f;
			shape->local.scale = static_cast<float>(instruction.size);
			shape->local.translate = { posX, posY, instruction.waterHeight };

			water->AttachChild(shape, true);
			built.emplace_back(shape, &instruction);

			block->waterAttached = true;
		}
	}

	func(block);

	if (!attaching || !block->waterAttached)
		return;

	for (auto& [shape, instruction] : built) {
		waterSystem->InitializeWater(shape, instruction->form.ptr, instruction->waterHeight, nullptr, true, false);

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
	const auto& singleton = globals::features::unifiedWater;
	if (singleton.flowmap) {
		// ObjectUV.xyz below, xy contains width and height, z contains mesh scale
		// Previously flowmap size was in x, yz contained flowmap offset for water displacement mesh
		*singleton.gFlowMapSize = singleton.flowmap->GetWidth();                                            // ObjectUV.x
		singleton.gDisplacementMeshFlowCellOffset->x = static_cast<float>(singleton.flowmap->GetHeight());  // ObjectUV.y
		singleton.gDisplacementMeshFlowCellOffset->y = 1.0f - pass->geometry->local.scale;                  // ObjectUV.z (counters 1 - x in SetupGeometry)

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().properties[1].get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			int32_t x, y;
			Util::WorldToCell(pass->geometry->world.translate, x, y);
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

	func(waterShader, pass);
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
