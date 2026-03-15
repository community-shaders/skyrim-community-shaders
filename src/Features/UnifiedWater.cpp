#include "UnifiedWater.h"

#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "ShaderCache.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes)

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

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Regenerate Flowmap") && flowmap) {
			if (flowmap->RegenerateAndLoadFlowmap())
				SetFlowmapTex();
		}

		if (ImGui::Button("Regenerate Caches") && waterCache)
			waterCache->RegenerateCaches();

		ImGui::TreePop();
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
	if (!nif || nif->GetChildren().empty() || !nif->GetChildren().front()->AsNode() || nif->GetChildren().front()->AsNode()->GetChildren().empty()) {
		logger::error("[Unified Water] Invalid water mesh hierarchy");
		return;
	}
	const auto waterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	if (!waterShape) {
		logger::error("[Unified Water] Water mesh does not contain valid TriShape");
		return;
	}
	waterMesh = RE::NiPointer(waterShape);
	logger::debug("[Unified Water] Water mesh loaded");

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\optimisedwatermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load optimised water mesh");
		return;
	}
	if (!nif || nif->GetChildren().empty() || !nif->GetChildren().front()->AsNode() || nif->GetChildren().front()->AsNode()->GetChildren().empty()) {
		logger::error("[Unified Water] Invalid optimised water mesh hierarchy");
		return;
	}
	const auto optimisedWaterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	if (!optimisedWaterShape) {
		logger::error("[Unified Water] Optimised water mesh does not contain valid TriShape");
		return;
	}
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

	if (!gFlowMapSourceTex || !gFlowMapSize) {
		logger::error("[Unified Water] Global pointers not initialized");
		return;
	}

	*gFlowMapSourceTex = tex;
	*gFlowMapSize = flowmap->GetWidth();

	logger::debug("[Unified Water] [Flowmap] Texture set");
}

void UnifiedWater::PostPostLoad()
{
	stl::detour_thunk<TES_SetWorldSpace>(REL::RelocationID(13170, 13315));
	stl::detour_thunk<TES_DestroySkyCell>(REL::RelocationID(20029, 20463));

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
				waterSystem->RemoveWater(child.get());
				water->DetachChild(child.get());
			}
		}

		attaching = true;

		const auto node = block->node;
		const auto lodLevel = node->GetLODLevel();
		const auto worldSpace = block->node->manager->worldSpace;

		const auto instructions = singleton.waterCache->GetInstructions(worldSpace, lodLevel, node->baseCellX, node->baseCellY);
		if (!instructions) {
			logger::warn("[Unified Water] No instructions found for {} chunk at {}, {}", worldSpace->GetFormEditorID(), node->baseCellX, node->baseCellY);
			func(block);
			return;
		}

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const auto targetShape = lodLevel > 4 || singleton.settings.UseOptimisedMeshes ? singleton.optimisedWaterMesh : singleton.waterMesh;
			RE::BSTriShape* shape = targetShape->CreateClone(cloningProcess)->AsTriShape();

			const auto posX = (instruction.x - node->baseCellX) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->baseCellY) * 4096.0f + instruction.size * 2048.0f;
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
		waterSystem->AddWater(shape, instruction->form.ptr, instruction->waterHeight, nullptr, true, false);

		if (const auto prop = shape->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
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
		if (!waterSystem->waterObjects.empty()) {
			waterSystem->waterObjects.pop_back();
		}
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

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
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
