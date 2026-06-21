#include "LightPicker.h"
#include "EditorWindow.h"

#include "RE/B/bhkPickData.h"
#include "RE/N/NiCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TES.h"
#include "RE/T/TESHavokUtilities.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectSTAT.h"

#include <imgui.h>

namespace
{
	// Skyrim world units -> Havok world units. bhkPickData rays are in Havok space.
	constexpr float kSkyrimToHavok = 0.0142875f;
	// Ray length in Skyrim units; long enough to reach any visible mesh.
	constexpr float kRayLengthSkyrim = 100000.0f;

	/** @brief True if the object is an editor marker (a STAT flagged "Is Marker"), which has no visible mesh and only clutters picks. */
	bool IsEditorMarker(const RE::TESBoundObject* baseObj)
	{
		const auto* stat = baseObj ? baseObj->As<RE::TESObjectSTAT>() : nullptr;
		return stat && (stat->GetFormFlags() & RE::TESObjectSTAT::RecordFlags::kIsMarker) != 0;
	}
}

void LightPicker::PopulateFromRef(PickedMesh& out, RE::TESObjectREFR* refr, RE::TESBoundObject* baseObj)
{
	out.refrHandle = refr->GetHandle();
	out.baseFormId = baseObj->formID;
	out.editorId = clib_util::editorID::get_editorID(baseObj);
	if (auto* model = baseObj->As<RE::TESModel>()) {
		if (const char* path = model->GetModel())
			out.modelPath = path;
	}
	if (const auto* file = baseObj->GetFile(0))
		out.sourcePlugin = file->fileName;
	out.refFormEntry = FormatRefFormEntry(refr);
	out.valid = true;
}

std::string LightPicker::FormatFormEntry(RE::FormID formId, std::string_view ownerPlugin)
{
	constexpr RE::FormID kRelativeFormIdMask = 0x00FFFFFF;  // strips the load-order mod index
	return fmt::format("0x{:X}~{}", formId & kRelativeFormIdMask, ownerPlugin);
}

std::string LightPicker::FormatRefFormEntry(RE::TESObjectREFR* refr)
{
	if (!refr)
		return {};
	const auto* ownerFile = refr->GetDescriptionOwnerFile();
	if (!ownerFile || !ownerFile->fileName)
		return {};
	return FormatFormEntry(refr->formID, ownerFile->fileName);
}

RE::NiCamera* LightPicker::GetPlayerNiCamera()
{
	auto* playerCamera = RE::PlayerCamera::GetSingleton();
	if (!playerCamera || !playerCamera->cameraRoot)
		return nullptr;
	// The world NiCamera lives among the camera root's children.
	for (auto& child : playerCamera->cameraRoot->GetChildren()) {
		if (auto* camera = netimmerse_cast<RE::NiCamera*>(child.get()))
			return camera;
	}
	return nullptr;
}

LightPicker::PickedMesh LightPicker::ResolveUnderCursor(bool logResult)
{
	PickedMesh out;

	auto* niCamera = GetPlayerNiCamera();
	if (!niCamera)
		return out;

	const ImVec2 mouse = ImGui::GetMousePos();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (display.x <= 0.0f || display.y <= 0.0f)
		return out;

	RE::NiPoint3 origin, dir;
	if (!niCamera->WindowPointToRay(static_cast<std::int32_t>(mouse.x), static_cast<std::int32_t>(mouse.y),
			origin, dir, display.x, display.y))
		return out;
	dir.Unitize();

	const RE::NiPoint3 end = origin + dir * kRayLengthSkyrim;

	RE::bhkPickData pick{};
	pick.rayInput.from = RE::hkVector4(origin.x * kSkyrimToHavok, origin.y * kSkyrimToHavok, origin.z * kSkyrimToHavok, 0.0f);
	pick.rayInput.to = RE::hkVector4(end.x * kSkyrimToHavok, end.y * kSkyrimToHavok, end.z * kSkyrimToHavok, 0.0f);

	auto* tes = RE::TES::GetSingleton();
	if (!tes)
		return out;
	tes->Pick(pick);

	if (!pick.rayOutput.rootCollidable)
		return out;

	RE::TESObjectREFR* refr = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
	if (!refr)
		return out;

	auto* baseObj = refr->GetObjectReference();
	if (!baseObj)
		return out;

	// Editor markers carry Havok geometry, so the raycast can land on them; skip so they aren't picked.
	if (IsEditorMarker(baseObj))
		return out;  // out.valid is still false

	PopulateFromRef(out, refr, baseObj);

	if (logResult)
		logger::info("[LightPicker] Hit ref 0x{:08X} '{}' model '{}' plugin '{}'",
			refr->GetFormID(), out.editorId, out.modelPath, out.sourcePlugin);
	return out;
}

LightPicker::PickedMesh LightPicker::ResolveNearestToCursor()
{
	PickedMesh out;

	auto* niCamera = GetPlayerNiCamera();
	if (!niCamera)
		return out;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return out;

	auto* cell = player->GetParentCell();
	if (!cell)
		return out;

	const ImVec2 cursor = ImGui::GetMousePos();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (display.x <= 0.0f || display.y <= 0.0f)
		return out;

	static constexpr float kSearchRadius = 5000.0f;          // Skyrim units (~50 m)
	static constexpr float kScreenThreshSq = 64.0f * 64.0f;  // pixels

	float bestDistSq = kScreenThreshSq;
	RE::TESObjectREFR* bestRef = nullptr;

	cell->ForEachReferenceInRange(player->GetPosition(), kSearchRadius,
		[&](RE::TESObjectREFR* refr) -> RE::BSContainer::ForEachResult {
			if (!refr || refr->IsDisabled() || refr->IsDeleted())
				return RE::BSContainer::ForEachResult::kContinue;

			float sx, sy, sz;
			if (!niCamera->WorldPtToScreenPt3(refr->GetPosition(), sx, sy, sz, 1e-5f) || sz <= 0.0f)
				return RE::BSContainer::ForEachResult::kContinue;

			// WorldPtToScreenPt3: (0,0) = bottom-left, (1,1) = top-right → flip y for ImGui.
			const float pixX = sx * display.x;
			const float pixY = (1.0f - sy) * display.y;
			const float dx = pixX - cursor.x;
			const float dy = pixY - cursor.y;
			const float distSq = dx * dx + dy * dy;

			if (distSq < bestDistSq) {
				if (IsEditorMarker(refr->GetObjectReference()))
					return RE::BSContainer::ForEachResult::kContinue;
				bestDistSq = distSq;
				bestRef = refr;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});

	if (!bestRef)
		return out;

	auto* baseObj = bestRef->GetObjectReference();
	if (!baseObj)
		return out;

	// Discard if the collision raycast also resolves this same ref: it has Havok geometry and is
	// reachable in normal collision mode.
	{
		PickedMesh collisionHit = ResolveUnderCursor(false);
		if (collisionHit.valid && collisionHit.refrHandle == bestRef->GetHandle())
			return out;  // out.valid is still false
	}

	PopulateFromRef(out, bestRef, baseObj);

	logger::info("[LightPicker] Effect-pick ref 0x{:08X} '{}' model '{}' plugin '{}'",
		bestRef->GetFormID(), out.editorId, out.modelPath, out.sourcePlugin);
	return out;
}

void LightPicker::BeginPick()
{
	picking = true;
	result = {};
	InvalidateHover();
	logger::info("[LightPicker] Pick mode started");
}

void LightPicker::Cancel()
{
	if (picking)
		logger::info("[LightPicker] Pick mode cancelled");
	picking = false;
}

void LightPicker::InvalidateHover()
{
	hoverMesh = {};
	lastMouseX = -1.f;
	lastMouseY = -1.f;
}

void LightPicker::Update()
{
	if (!picking)
		return;

	const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	if (escapePressed || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		if (escapePressed)
			EditorWindow::GetSingleton()->suppressNextEditorEscape = true;
		Cancel();
		return;
	}

	// Ignore clicks consumed by the UI; only world clicks pick a mesh.
	if (ImGui::GetIO().WantCaptureMouse) {
		hoverMesh = {};
		return;
	}

	// Update hover mesh when the cursor moves.
	const ImVec2 mouse = ImGui::GetMousePos();
	if (mouse.x != lastMouseX || mouse.y != lastMouseY) {
		lastMouseX = mouse.x;
		lastMouseY = mouse.y;
		hoverMesh = (pickMode == PickMode::kEffect) ? ResolveNearestToCursor() : ResolveUnderCursor(false);
	}

	if (hoverMesh.valid) {
		ImGui::BeginTooltip();
		if (!hoverMesh.editorId.empty())
			ImGui::Text("EditorID: %s", hoverMesh.editorId.c_str());
		if (!hoverMesh.modelPath.empty())
			ImGui::Text("Mesh: %s", hoverMesh.modelPath.c_str());
		ImGui::Text("Base FormID: 0x%08X", hoverMesh.baseFormId);
		if (!hoverMesh.refFormEntry.empty())
			ImGui::Text("Reference FormID: %s", hoverMesh.refFormEntry.c_str());
		if (!hoverMesh.sourcePlugin.empty())
			ImGui::Text("Plugin: %s", hoverMesh.sourcePlugin.c_str());
		ImGui::EndTooltip();
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		// Reuse the hover result if the mouse didn't move between the last hover update and click.
		PickedMesh hit = hoverMesh.valid ? hoverMesh :
		                                   (pickMode == PickMode::kEffect ? ResolveNearestToCursor() : ResolveUnderCursor());
		if (hit.valid) {
			result = hit;
			picking = false;
			hoverMesh = {};
		}
		// Miss: stay in pick mode so the user can try again.
	}
}

LightPicker::PickedMesh LightPicker::TakeResult()
{
	PickedMesh out = result;
	result = {};
	return out;
}
