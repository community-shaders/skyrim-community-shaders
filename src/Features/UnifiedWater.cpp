#include "UnifiedWater.h"

#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Util.h"

#include <imgui_internal.h>
#include <cmath>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes)

static bool IsChildWorldSpace(const RE::TESWorldSpace* ws)
{
	return ws && ws->parentWorld &&
	       ws->parentUseFlags.all(RE::TESWorldSpace::ParentUseFlag::kUseLODData);
}

// Engine behavior: CellState value 6 is the transition/attached state.
static constexpr auto kTransitionAttachedCellState = static_cast<RE::TESObjectCELL::CellState>(6);
static constexpr uint32_t kPostWorldspaceCullFrames = 240;

static bool ShouldCullAtCell(const RE::TES* tes, int32_t cellX, int32_t cellY, bool* isInGrid = nullptr)
{
	if (isInGrid)
		*isInGrid = false;
	if (!tes || !tes->gridCells)
		return false;

	const auto& gridCells = tes->gridCells;
	const int32_t offsetX = tes->currentGridX - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t offsetY = tes->currentGridY - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t length = static_cast<int32_t>(gridCells->length);

	const int32_t x = cellX - offsetX;
	const int32_t y = cellY - offsetY;
	if (x < 0 || y < 0 || x >= length || y >= length)
		return false;

	if (isInGrid)
		*isInGrid = true;

	if (const auto cell = gridCells->GetCell(x, y)) {
		return cell->cellState.any(RE::TESObjectCELL::CellState::kAttached, kTransitionAttachedCellState);
	}

	return false;
}

static void ClearWaterNodeChildren(RE::NiNode* node, RE::TESWaterSystem* waterSystem)
{
	if (!node)
		return;

	auto count = node->GetChildren().size();
	while (count > 0) {
		const auto child = node->GetChildren()[count - 1];
		if (const auto childNode = child ? child->AsNode() : nullptr)
			ClearWaterNodeChildren(childNode, waterSystem);

		if (child && waterSystem) {
			waterSystem->RemoveWater(child.get());
		}

		node->DetachChildAt(--count);
	}
}

static bool InstructionCoversCell(const WaterCache::Instruction& instruction, int32_t cellX, int32_t cellY)
{
	return instruction.form.ptr &&
	       cellX >= instruction.x &&
	       cellY >= instruction.y &&
	       cellX < instruction.x + static_cast<int32_t>(instruction.size) &&
	       cellY < instruction.y + static_cast<int32_t>(instruction.size);
}

static bool InstructionsCoverObject(const std::vector<WaterCache::Instruction>& instructions, const RE::NiAVObject* object)
{
	if (!object)
		return false;

	int32_t cellX, cellY;
	Util::WorldToCell(object->world.translate, cellX, cellY);
	for (const auto& instruction : instructions) {
		if (InstructionCoversCell(instruction, cellX, cellY))
			return true;
	}

	return false;
}

static void DetachAllChildOccurrences(RE::NiNode* node, const RE::NiAVObject* childToDetach)
{
	if (!node || !childToDetach)
		return;

	auto count = node->GetChildren().size();
	while (count > 0) {
		const auto child = node->GetChildren()[count - 1];
		if (child.get() == childToDetach) {
			node->DetachChildAt(--count);
		} else {
			count--;
		}
	}
}

struct WaterPositionKey
{
	int32_t x = 0;
	int32_t y = 0;
	int32_t z = 0;
	int32_t scale = 0;
};

static bool operator==(const WaterPositionKey& lhs, const WaterPositionKey& rhs)
{
	return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.scale == rhs.scale;
}

struct WaterPositionKeyHash
{
	size_t operator()(const WaterPositionKey& key) const noexcept
	{
		size_t hash = std::hash<int32_t>{}(key.x);
		hash ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int32_t>{}(key.scale) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
	}
};

static int32_t QuantizeWaterPosition(float value)
{
	return static_cast<int32_t>(std::lround(value));
}

static WaterPositionKey GetWaterPositionKey(const RE::NiAVObject* object)
{
	if (!object)
		return {};

	return {
		QuantizeWaterPosition(object->world.translate.x),
		QuantizeWaterPosition(object->world.translate.y),
		QuantizeWaterPosition(object->world.translate.z),
		QuantizeWaterPosition(object->world.scale * 1000.0f),
	};
}

static bool IsChildOfNode(const RE::NiAVObject* object, const RE::NiNode* root)
{
	if (!object || !root)
		return false;

	for (auto parent = object->parent; parent; parent = parent->parent) {
		if (parent == root)
			return true;
	}

	return object == root;
}

static bool IsSameOrDescendantOf(const RE::NiAVObject* object, const RE::NiAVObject* root)
{
	if (!object || !root)
		return false;

	for (auto current = object; current; current = current->parent) {
		if (current == root)
			return true;
	}

	return false;
}

static RE::BSTriShape* SelectDuplicateWaterSystemShapeToRemove(RE::BSTriShape* existing, RE::BSTriShape* candidate, RE::NiNode* lodRoot)
{
	if (!existing)
		return candidate;
	if (!candidate)
		return existing;

	const bool existingIsLOD = IsChildOfNode(existing, lodRoot);
	const bool candidateIsLOD = IsChildOfNode(candidate, lodRoot);
	if (existingIsLOD != candidateIsLOD)
		return existingIsLOD ? existing : candidate;

	return candidate;
}

static bool IsPlacedWaterCandidate(const RE::TESObjectREFR* ref);
static bool IsPlacedWaterWhitelisted(const RE::BSTriShape* shape);
static RE::TESObjectREFR* FindRefForWaterGeometry(RE::NiAVObject* object);
static bool TryWhitelistPlacedWaterOverlay(RE::BSTriShape* shape, const char* source);

static void RemoveDuplicateWaterSystemObjects(RE::TESWaterSystem* waterSystem, RE::NiNode* lodRoot)
{
	if (!waterSystem)
		return;

	static thread_local std::unordered_map<WaterPositionKey, RE::BSTriShape*, WaterPositionKeyHash> shapeByPosition;
	static thread_local std::vector<RE::BSTriShape*> duplicateShapes;

	shapeByPosition.clear();
	duplicateShapes.clear();

	const auto objectCount = waterSystem->waterObjects.size();
	if (shapeByPosition.bucket_count() < objectCount)
		shapeByPosition.reserve(objectCount);
	if (duplicateShapes.capacity() < objectCount)
		duplicateShapes.reserve(objectCount);

	for (const auto& waterObject : waterSystem->waterObjects) {
		const auto shape = waterObject ? waterObject->shape.get() : nullptr;
		if (!shape)
			continue;
		if (IsPlacedWaterWhitelisted(shape) || (!IsChildOfNode(shape, lodRoot) && TryWhitelistPlacedWaterOverlay(shape, "RemoveDuplicateWaterSystemObjects")))
			continue;

		const auto key = GetWaterPositionKey(shape);
		const auto [it, inserted] = shapeByPosition.try_emplace(key, shape);
		if (inserted)
			continue;

		const auto existing = it->second;
		const auto duplicate = SelectDuplicateWaterSystemShapeToRemove(existing, shape, lodRoot);
		if (duplicate == existing)
			it->second = shape;

		duplicateShapes.push_back(duplicate);
	}

	for (const auto shape : duplicateShapes) {
		if (!shape)
			continue;

		shape->SetAppCulled(true);
		waterSystem->RemoveWater(shape);
	}
}

static bool HasRegisteredWaterObjectInCell(RE::TESWaterSystem* waterSystem, int32_t cellX, int32_t cellY, const RE::NiAVObject* ignoredObject)
{
	if (!waterSystem)
		return false;

	for (const auto& waterObject : waterSystem->waterObjects) {
		const auto shape = waterObject ? waterObject->shape.get() : nullptr;
		if (!shape || IsSameOrDescendantOf(shape, ignoredObject) || shape->GetAppCulled())
			continue;

		int32_t x, y;
		Util::WorldToCell(shape->world.translate, x, y);
		if (x != cellX || y != cellY)
			continue;

		const bool ignoredPlacedWater = IsPlacedWaterWhitelisted(shape) || TryWhitelistPlacedWaterOverlay(shape, "HasRegisteredWaterObjectInCell");
		if (ignoredPlacedWater)
			continue;

		return true;
	}

	return false;
}

struct PreservedWaterShape
{
	RE::NiPointer<RE::BSTriShape> shape;
	RE::TESWaterForm* form = nullptr;
	float waterHeight = 0.0f;
};

struct PreservedWaterSubtree
{
	RE::NiPointer<RE::NiAVObject> root;
	std::vector<PreservedWaterShape> shapes;
};

static bool IsWaterTriShape(RE::NiAVObject* object)
{
	const auto shape = object ? object->AsTriShape() : nullptr;
	if (!shape)
		return false;

	const auto prop = shape->GetGeometryRuntimeData().shaderProperty.get();
	return prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get();
}

static RE::TESWaterForm* SelectWaterFormForPreservedShape(
	const std::vector<WaterCache::Instruction>& instructions,
	const RE::TESWorldSpace* worldSpace,
	const RE::NiAVObject* object)
{
	RE::TESWaterForm* bestForm = nullptr;
	int64_t bestDistance = std::numeric_limits<int64_t>::max();

	int32_t objectCellX = 0;
	int32_t objectCellY = 0;
	Util::WorldToCell(object->world.translate, objectCellX, objectCellY);

	for (const auto& instruction : instructions) {
		if (!instruction.form.ptr)
			continue;

		const auto centerX = instruction.x + static_cast<int32_t>(instruction.size / 2);
		const auto centerY = instruction.y + static_cast<int32_t>(instruction.size / 2);
		const auto dx = static_cast<int64_t>(centerX) - objectCellX;
		const auto dy = static_cast<int64_t>(centerY) - objectCellY;
		const auto distance = dx * dx + dy * dy;
		if (distance < bestDistance) {
			bestDistance = distance;
			bestForm = instruction.form.ptr;
		}
	}

	return bestForm ? bestForm : worldSpace ? worldSpace->worldWater : nullptr;
}

static void ApplyUnifiedWaterFlags(RE::BSTriShape* shape, RE::TESWaterForm* form)
{
	if (!shape)
		return;

	const auto prop = shape->GetGeometryRuntimeData().shaderProperty.get();
	if (!prop || prop->GetRTTI() != globals::rtti::BSWaterShaderPropertyRTTI.get())
		return;

	const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
	REX::EnumSet waterFlags = static_cast<RE::BSWaterShaderProperty::WaterFlag>(0b10000100);
	waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseCubemapReflections;
	waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseReflections;
	if (form) {
		if (form->flags.any(RE::TESWaterForm::Flag::kEnableFlowmap))
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kEnableFlowmap;
		if (form->flags.any(RE::TESWaterForm::Flag::kBlendNormals))
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kBlendNormals;
	}
	waterShaderProp->waterFlags = waterFlags;
	waterShaderProp->plane.normal = { 0.0f, 0.0f, 1.0f };
	waterShaderProp->plane.constant = shape->world.translate.z;
}

static void CollectUncoveredWaterShapes(
	RE::NiAVObject* object,
	const std::vector<WaterCache::Instruction>& instructions,
	const RE::TESWorldSpace* worldSpace,
	std::vector<PreservedWaterShape>& preserved)
{
	if (!object)
		return;

	if (IsWaterTriShape(object) && !InstructionsCoverObject(instructions, object)) {
		auto shape = RE::NiPointer<RE::BSTriShape>(object->AsTriShape());
		if (const auto form = SelectWaterFormForPreservedShape(instructions, worldSpace, shape.get())) {
			preserved.push_back({ shape, form, shape->world.translate.z });
		}
	}

	const auto node = object->AsNode();
	if (!node)
		return;

	for (const auto& child : node->GetChildren())
		CollectUncoveredWaterShapes(child.get(), instructions, worldSpace, preserved);
}

static void SetSubtreeAppCulled(RE::NiAVObject* object, bool culled)
{
	if (!object)
		return;

	object->SetAppCulled(culled);
	if (const auto node = object->AsNode()) {
		for (const auto& child : node->GetChildren())
			SetSubtreeAppCulled(child.get(), culled);
	}
}

static void PreserveUncoveredWaterSubtrees(
	RE::NiNode* water,
	RE::TESWaterSystem* waterSystem,
	const std::vector<WaterCache::Instruction>& instructions,
	const RE::TESWorldSpace* worldSpace,
	std::vector<PreservedWaterSubtree>& preserved)
{
	if (!water || !waterSystem)
		return;

	auto count = water->GetChildren().size();
	while (count > 0) {
		const auto child = water->GetChildren()[count - 1];
		if (!child) {
			count--;
			continue;
		}

		std::vector<PreservedWaterShape> shapes;
		CollectUncoveredWaterShapes(child.get(), instructions, worldSpace, shapes);
		if (shapes.empty()) {
			count--;
			continue;
		}

		for (auto& preservedShape : shapes) {
			waterSystem->RemoveWater(preservedShape.shape.get());
			preservedShape.shape->SetAppCulled(false);
		}

		SetSubtreeAppCulled(child.get(), false);
		preserved.push_back({ RE::NiPointer<RE::NiAVObject>(child.get()), std::move(shapes) });
		water->DetachChildAt(--count);
	}
}

static std::shared_mutex s_placedWaterMutex;
static std::unordered_set<RE::FormID> s_placedWaterWhitelist;

static bool IsTemporaryFormID(RE::FormID formID)
{
	return (formID & 0xFF000000) == 0xFF000000;
}

static bool IsPlacedWaterCandidate(const RE::TESObjectREFR* ref)
{
	const auto base = ref ? ref->GetBaseObject() : nullptr;
	if (!base)
		return false;

	// Procedural close water can resolve through FindReferenceFor3D as an Activator
	// with temporary FF... forms. Those are real replacement water surfaces and must
	// still be allowed to cull UW LOD, otherwise interior/exterior flicker returns.
	if (IsTemporaryFormID(ref->formID) || IsTemporaryFormID(base->formID))
		return false;

	const auto formType = base->GetFormType();
	return formType == RE::FormType::Activator || formType == RE::FormType::Static;
}

static bool IsPlacedWaterWhitelisted(const RE::BSTriShape* shape)
{
	if (!shape)
		return false;

	const auto ref = FindRefForWaterGeometry(const_cast<RE::BSTriShape*>(shape));
	if (!ref || !IsPlacedWaterCandidate(ref))
		return false;

	std::shared_lock lock(s_placedWaterMutex);
	return s_placedWaterWhitelist.contains(ref->formID);
}

static RE::TESObjectREFR* FindRefForWaterGeometry(RE::NiAVObject* object)
{
	for (auto current = object; current; current = current->parent) {
		if (const auto ref = RE::TESObjectREFR::FindReferenceFor3D(current))
			return ref;
	}

	return nullptr;
}

static bool TryWhitelistPlacedWaterOverlay(RE::BSTriShape* shape, const char* source)
{
	if (!shape)
		return false;

	const auto ref = FindRefForWaterGeometry(shape);
	if (!ref || !IsPlacedWaterCandidate(ref))
		return false;

	(void)source;
	std::unique_lock lock(s_placedWaterMutex);
	s_placedWaterWhitelist.insert(ref->formID);

	return true;
}

struct CullCompletionState
{
	bool foundInGridChild = false;
	bool foundAttachedCell = false;
	bool hasPotentiallyAttachableChild = false;

	bool IsComplete() const
	{
		return !hasPotentiallyAttachableChild && (foundAttachedCell || !foundInGridChild);
	}
};

// Cull waterParent children using tes->gridCells attachment state.
// Pass tes explicitly when globals::game::tes is not ready (e.g., TES_SetWorldSpace).
static CullCompletionState CullWaterParentByGridCells(RE::NiNode* waterParent, RE::TES* tes = nullptr)
{

	if (!tes)
		tes = globals::game::tes;
	if (!tes || !waterParent)
		return {};

	CullCompletionState state;

	for (const auto& child : waterParent->GetChildren()) {
		if (!child)
			continue;

		int32_t x, y;
		Util::WorldToCell(child->world.translate, x, y);

		bool isInGrid = false;
		const bool wantsCull = ShouldCullAtCell(tes, x, y, &isInGrid);
		const bool hasReplacement = !wantsCull || HasRegisteredWaterObjectInCell(globals::game::waterSystem, x, y, child.get());
		const bool cull = wantsCull && hasReplacement;

		if (isInGrid)
			state.foundInGridChild = true;
		if (cull)
			state.foundAttachedCell = true;
		else if (isInGrid)
			state.hasPotentiallyAttachableChild = true;
		child->SetAppCulled(cull);
	}

	return state;
}

// Cull all tiles under every water LOD parent.
static CullCompletionState CullAllWaterLODParents(RE::NiNode* waterLOD, RE::TES* tes = nullptr)
{
	if (!waterLOD)
		return {};

	CullCompletionState aggregate;

	for (const auto& waterParentPtr : waterLOD->GetChildren()) {
		if (!waterParentPtr)
			continue;
		const auto waterParent = waterParentPtr->AsNode();
		if (!waterParent)
			continue;
		const auto state = CullWaterParentByGridCells(waterParent, tes);
		aggregate.foundInGridChild = aggregate.foundInGridChild || state.foundInGridChild;
		aggregate.foundAttachedCell = aggregate.foundAttachedCell || state.foundAttachedCell;
		aggregate.hasPotentiallyAttachableChild = aggregate.hasPotentiallyAttachableChild || state.hasPotentiallyAttachableChild;
	}

	return aggregate;
}

void UnifiedWater::TryCompleteDeferredChildWorldspaceCull(RE::TES* tes)
{
	if (!pendingChildWsCull.load(std::memory_order_acquire))
		return;

	if (!IsChildWorldSpace(currentPlayerWorldSpace.load(std::memory_order_acquire)) || !gWaterLOD || !*gWaterLOD) {
		pendingChildWsCull.store(false, std::memory_order_release);
		return;
	}

	if (!tes)
		tes = cachedTes.load(std::memory_order_acquire);
	if (!tes || !tes->gridCells) {
		if (transitionCullFrames.load(std::memory_order_acquire) == 0)
			pendingChildWsCull.store(false, std::memory_order_release);
		return;
	}

	const auto state = CullAllWaterLODParents(*gWaterLOD, tes);
	if (state.IsComplete() || transitionCullFrames.load(std::memory_order_acquire) == 0)
		pendingChildWsCull.store(false, std::memory_order_release);
}

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

	const float scale = Util::GetUIScale();
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const auto& style = ImGui::GetStyle();

	// Stack below shader compilation window if it's visible this frame
	float vOffset = 0.0f;
	if (auto* shaderWin = ImGui::FindWindowByName("ShaderCompilationInfo")) {
		if (shaderWin->Active) {
			vOffset = (shaderWin->Pos.y + shaderWin->Size.y) - pos + style.ItemSpacing.y;
		}
	}
	// Also stack below shader blocking overlay if visible
	if (auto* blockingWin = ImGui::FindWindowByName("ShaderBlockingInfo")) {
		if (blockingWin->Active) {
			float blockingBottom = (blockingWin->Pos.y + blockingWin->Size.y) - pos + style.ItemSpacing.y;
			if (blockingBottom > vOffset)
				vOffset = blockingBottom;
		}
	}

	const auto snapshot = waterCache->GetBuildProgressSnapshot();

	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	if (waterCache->IsBuildRunning()) {
		auto progressTitle = fmt::format("Generating Water Cache:");
		auto percent = static_cast<float>(snapshot.completed) / static_cast<float>(snapshot.total);
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", snapshot.completed, snapshot.total, 100 * percent);

		ImGui::SetNextWindowPos(ImVec2(pos, pos + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());

		ImGui::End();
	} else if (waterCache->HasBuildFailed()) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos + vOffset));
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
	if (!REL::Module::IsVR()) {
		stl::detour_thunk<TES_SetWorldSpace>(REL::RelocationID(13170, 13315));
		stl::detour_thunk<TES_DestroySkyCell>(REL::RelocationID(20029, 20463));
	}

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
	const bool enteringChild = IsChildWorldSpace(worldSpace);

	// Set before func so attachment hooks fired inside func see the new worldspace.
	auto& uw = globals::features::unifiedWater;
	uw.currentPlayerWorldSpace.store(worldSpace, std::memory_order_release);
	uw.cachedTes.store(tes, std::memory_order_release);
	if (!enteringChild)
		uw.pendingChildWsCull.store(false, std::memory_order_release);  // leaving child WS: discard any stale pending cull
	if (worldSpace && isExterior)
		uw.transitionCullFrames.store(kPostWorldspaceCullFrames, std::memory_order_release);

	func(tes, worldSpace, isExterior);

	if (!uw.waterCache) {
		uw.pendingChildWsCull.store(false, std::memory_order_release);
		uw.transitionCullFrames.store(0, std::memory_order_release);
		return;
	}

	uw.waterCache->SetCurrentWorldSpace(worldSpace);

	if (worldSpace && isExterior) {
		if (const auto waterSystem = globals::game::waterSystem)
			waterSystem->Enable();

		if (uw.gWaterLOD && *uw.gWaterLOD && tes && tes->gridCells)
			CullAllWaterLODParents(*uw.gWaterLOD, tes);

		uw.transitionCullFrames.store(kPostWorldspaceCullFrames, std::memory_order_release);
	}

	if (enteringChild) {
		// BGSTerrainBlock_Attach calls Enable() on block attach.
		// Child-worldspace transitions can keep old LOD blocks attached, so re-enable here.
		if (const auto waterSystem = globals::game::waterSystem)
			waterSystem->Enable();

		// Try an immediate cull with tes (globals::game::tes may still be null).
		// Newly transitioned cells are often not attached yet, so deferred retries are still needed.
		if (uw.gWaterLOD && *uw.gWaterLOD && tes && tes->gridCells)
			CullAllWaterLODParents(*uw.gWaterLOD, tes);

		// Keep deferred retries enabled until attached cells are observed and culled.
		uw.pendingChildWsCull.store(true, std::memory_order_release);
	}
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	auto& uw = globals::features::unifiedWater;
	uw.currentPlayerWorldSpace.store(nullptr, std::memory_order_release);
	uw.pendingChildWsCull.store(false, std::memory_order_release);
	uw.transitionCullFrames.store(0, std::memory_order_release);
	uw.cachedTes.store(nullptr, std::memory_order_release);
	if (!uw.waterCache)
		return;

	uw.waterCache->SetCurrentWorldSpace(nullptr);
}

void UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent)
{
	if (!node || !waterParent)
		return;

	if (node->GetLODLevel() != 4)
		return;

	CullWaterParentByGridCells(waterParent);
}

void UnifiedWater::BGSTerrainBlock_Attach::thunk(RE::BGSTerrainBlock* block)
{
	const auto waterSystem = globals::game::waterSystem;
	auto& uw = globals::features::unifiedWater;

	if (!waterSystem || !uw.waterCache || !uw.gWaterLOD || !*uw.gWaterLOD) {
		func(block);
		return;
	}

	// Additional game-thread retry path for deferred child-WS cull completion.
	uw.TryCompleteDeferredChildWorldspaceCull(uw.cachedTes.load(std::memory_order_acquire));
	if (uw.transitionCullFrames.load(std::memory_order_acquire) > 0)
		CullAllWaterLODParents(*uw.gWaterLOD, uw.cachedTes.load(std::memory_order_acquire));

	std::vector<std::pair<RE::BSTriShape*, const WaterCache::Instruction*>> built;
	std::vector<PreservedWaterSubtree> preserved;
	bool attaching = false;

	if (block && block->loaded && !block->attached && block->chunk && block->water) {
		const auto node = block->node;
		const auto worldSpace = node && node->manager ? node->manager->worldSpace : nullptr;
		if (!node || !worldSpace) {
			func(block);
			return;
		}

		const auto lodLevel = node->GetLODLevel();

		const auto instructions = uw.waterCache->GetInstructions(worldSpace, lodLevel, node->baseCellX, node->baseCellY);
		if (!instructions) {
			logger::warn("[Unified Water] No instructions found for {} chunk at {}, {}", worldSpace->GetFormEditorID(), node->baseCellX, node->baseCellY);
			func(block);
			return;
		}

		DetachAllChildOccurrences(*uw.gWaterLOD, block->water);
		block->chunk->DetachChild2(block->water);
		block->water->local.translate = block->chunk->local.translate;

		RE::NiUpdateData updateData;
		block->water->UpdateUpwardPass(updateData);

		const auto water = block->water;
		PreserveUncoveredWaterSubtrees(water, waterSystem, *instructions, worldSpace, preserved);
		ClearWaterNodeChildren(water, waterSystem);
		block->waterAttached = !preserved.empty();
		for (auto& rescued : preserved) {
			if (rescued.root) {
				water->AttachChild(rescued.root.get(), true);
				SetSubtreeAppCulled(rescued.root.get(), false);
			}
		}

		attaching = true;

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const auto targetShape = lodLevel > 4 || uw.settings.UseOptimisedMeshes ? uw.optimisedWaterMesh : uw.waterMesh;
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

	if (!attaching)
		return;

	const bool hasManagedWater = !built.empty() || !preserved.empty();
	if (block)
		block->waterAttached = hasManagedWater;
	if (!hasManagedWater)
		return;

	for (auto& [shape, instruction] : built) {
		waterSystem->AddWater(shape, instruction->form.ptr, instruction->waterHeight, nullptr, true, false);

		ApplyUnifiedWaterFlags(shape, instruction->form.ptr);

		// Remove from WaterSystem, will manage it ourselves
		if (!waterSystem->waterObjects.empty()) {
			waterSystem->waterObjects.pop_back();
		}
	}
	for (auto& rescued : preserved) {
		if (rescued.root)
			SetSubtreeAppCulled(rescued.root.get(), false);

		for (auto& preservedShape : rescued.shapes) {
			if (!preservedShape.shape || !preservedShape.form)
				continue;

			waterSystem->AddWater(preservedShape.shape.get(), preservedShape.form, preservedShape.waterHeight, nullptr, true, false);
			ApplyUnifiedWaterFlags(preservedShape.shape.get(), preservedShape.form);
			preservedShape.shape->SetAppCulled(false);
		}
	}

	RemoveDuplicateWaterSystemObjects(waterSystem, *uw.gWaterLOD);
	if (block && block->water)
		DetachAllChildOccurrences(*uw.gWaterLOD, block->water);
	(*uw.gWaterLOD)->AttachChild(block->water, true);
	waterSystem->Enable();

	// BGSTerrainNode_UpdateWaterMeshSubVisibility never fires in child worldspaces.
	// Cull newly built tiles here; full deferred retries are handled by
	// TryCompleteDeferredChildWorldspaceCull().
	if (IsChildWorldSpace(uw.currentPlayerWorldSpace.load(std::memory_order_acquire))) {
		const auto tes = uw.cachedTes.load(std::memory_order_acquire);
		if (tes && tes->gridCells) {
			for (const auto& [shape, instruction] : built) {
				const bool wantsCull = ShouldCullAtCell(tes, instruction->x, instruction->y);
				const bool cull = wantsCull && HasRegisteredWaterObjectInCell(waterSystem, instruction->x, instruction->y, shape);
				shape->SetAppCulled(cull);
			}
		}
	}
}

void UnifiedWater::BGSTerrainBlock_Detach::thunk(RE::BGSTerrainBlock* block)
{
	auto& uw = globals::features::unifiedWater;
	const auto water = block ? block->water : nullptr;
	if (block)
		block->water = nullptr;

	func(block);

	if (block)
		block->water = water;

	if (water) {
		ClearWaterNodeChildren(water, globals::game::waterSystem);

		if (uw.gWaterLOD && *uw.gWaterLOD)
			DetachAllChildOccurrences(*uw.gWaterLOD, water);
		if (block)
			block->waterAttached = false;
	}
}

void UnifiedWater::BSWaterShader_SetupGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass)
{
	auto& uw = globals::features::unifiedWater;

	if (pass && pass->geometry) {
		// Re-stabilize BSWaterShaderProperty.plane every draw. After interior/exterior
		// transitions the cached plane can be stale for exactly one of two overlapping
		// water surfaces, which presents as heavy flicker rather than missing water.
		if (const auto prop = pass->geometry->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			const float waterHeight = pass->geometry->world.translate.z;

			waterShaderProp->plane.normal = { 0.0f, 0.0f, 1.0f };
			waterShaderProp->plane.constant = waterHeight;
		}
	}

	if (uw.flowmap && pass && pass->geometry) {
		// ObjectUV.xyz below, xy contains width and height, z contains mesh scale
		// Previously flowmap size was in x, yz contained flowmap offset for water displacement mesh
		*uw.gFlowMapSize = uw.flowmap->GetWidth();                                            // ObjectUV.x
		uw.gDisplacementMeshFlowCellOffset->x = static_cast<float>(uw.flowmap->GetHeight());  // ObjectUV.y
		uw.gDisplacementMeshFlowCellOffset->y = 1.0f - pass->geometry->local.scale;           // ObjectUV.z (counters 1 - x in SetupGeometry)

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			int32_t x, y;
			Util::WorldToCell(pass->geometry->world.translate, x, y);
			// CellTexCoordOffset.xyzw below - applies to non-displacement water only
			// xy is world cell flowmap based (0,0 is corner of flow map), zw is world cell
			// Funky maths here to counter what's being done in SetupGeometry
			// Previously these values were relative to the 5x5 flow grid centered on the player
			waterShaderProp->flowX = x + uw.flowmap->GetOffsetX();                                                     // CellTexCoordOffset.x
			waterShaderProp->flowY = y + uw.flowmap->GetOffsetY() + uw.flowmap->GetWidth() - uw.flowmap->GetHeight();  // CellTexCoordOffset.y
			waterShaderProp->cellX = x;                                                                                // CellTexCoordOffset.z
			waterShaderProp->cellY = y;                                                                                // CellTexCoordOffset.w
		}
	}

	func(waterShader, pass);
}

void UnifiedWater::TESWaterSystem_UpdateDisplacementMeshPosition::thunk(RE::TESWaterSystem* waterSystem)
{
	auto& uw = globals::features::unifiedWater;

	func(waterSystem);

	RemoveDuplicateWaterSystemObjects(waterSystem, uw.gWaterLOD ? *uw.gWaterLOD : nullptr);

	// Game-thread fallback for deferred child-worldspace cull completion.
	// Needed when entering child worldspaces with already-attached LOD blocks,
	// where BGSTerrainBlock_Attach/UpdateWaterMeshSubVisibility may not run.
	uw.TryCompleteDeferredChildWorldspaceCull(uw.cachedTes.load(std::memory_order_acquire));
	if (const auto remaining = uw.transitionCullFrames.load(std::memory_order_acquire); remaining > 0) {
		if (uw.gWaterLOD && *uw.gWaterLOD)
			CullAllWaterLODParents(*uw.gWaterLOD, uw.cachedTes.load(std::memory_order_acquire));
		uw.transitionCullFrames.store(remaining - 1, std::memory_order_release);
	}

	if (!uw.flowmap)
		return;

	const float posX = uw.gDisplacementMeshPos->x / 4096.0f;
	const float posY = uw.gDisplacementMeshPos->y / 4096.0f;
	const float offsetX = static_cast<float>(uw.flowmap->GetOffsetX());
	const float offsetY = static_cast<float>(uw.flowmap->GetOffsetY());
	const float height = static_cast<float>(uw.flowmap->GetHeight());

	// CellTexCoordOffset.xyzw below - applies to displacement water only
	// Previously the values were calculated relative to the 5x5 flow grid
	*uw.gDisplacementCellTexCoordOffset = float4(posX + offsetX, height - (posY + offsetY), posX, 1 - posY);
}
