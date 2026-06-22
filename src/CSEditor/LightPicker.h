#pragma once

#include "RE/B/BSCoreTypes.h"      // RE::FormID
#include "RE/B/BSPointerHandle.h"  // RE::ObjectRefHandle

#include <string>
#include <string_view>

namespace RE
{
	class NiCamera;
	class TESObjectREFR;
	class TESBoundObject;
}

/** @brief Resolves the mesh/reference under the cursor via a camera-through-cursor Havok raycast; isolated from LightEditor for testability. */
struct LightPicker
{
	/** @brief Identity of a picked mesh's reference and base object (valid==false until populated). */
	struct PickedMesh
	{
		RE::ObjectRefHandle refrHandle;  // safe across cell changes
		RE::FormID baseFormId = 0;       // base object FormID
		std::string editorId;            // base object EditorID (may be empty)
		std::string modelPath;           // base object .nif path (may be empty)
		std::string sourcePlugin;        // plugin defining the base FormID (may be empty)
		std::string refFormEntry;        // reference FormID in LP filter-list form ("0x...~Plugin.esp"); may be empty
		bool valid = false;
	};

	/** @brief Which mesh the picker targets: solid collision geometry, or the nearest on-screen effect mesh. */
	enum class PickMode
	{
		kCollision = 0,
		kEffect = 1
	};
	PickMode pickMode = PickMode::kCollision;  // persists across picks

	/** @brief Enters pick mode; while active, Update() watches for a world click. */
	void BeginPick();
	/** @brief Leaves pick mode without producing a result. */
	void Cancel();
	[[nodiscard]] bool IsPicking() const { return picking; }

	/** @brief Clears the cached hover hit so Update() recomputes next frame; call when pick mode changes to avoid a stale hit. */
	void InvalidateHover();

	/** @brief Runs once per frame: raycasts on a qualifying left-click and stores a result; handles right-click/ESC cancel. */
	void Update();

	/** @brief Returns and clears the last successful pick (valid==true at most once per pick). */
	[[nodiscard]] PickedMesh TakeResult();

	/** @brief Formats a reference into the LP filter-list entry string ("0x{relativeID}~Plugin.esp"), or "" if it has no owner file. */
	static std::string FormatRefFormEntry(RE::TESObjectREFR* refr);

	/** @brief Formats a FormID + owner plugin into an LP filter-list entry string ("0x{relativeID}~Plugin.esp"). */
	static std::string FormatFormEntry(RE::FormID formId, std::string_view ownerPlugin);

private:
	/** @brief Finds the world NiCamera among the player camera root's children, or nullptr. */
	static RE::NiCamera* GetPlayerNiCamera();
	/** @brief Raycasts through the cursor and resolves the collision mesh/ref under it. */
	static PickedMesh ResolveUnderCursor(bool logResult = true);
	/** @brief Resolves the on-screen ref nearest the cursor (effect-mesh pick), skipping collision-reachable ones. */
	static PickedMesh ResolveNearestToCursor();
	/** @brief Fills out's identifying fields (handle, FormID, EditorID, model, plugin) from a ref. */
	static void PopulateFromRef(PickedMesh& out, RE::TESObjectREFR* refr, RE::TESBoundObject* baseObj);

	bool picking = false;
	PickedMesh result;
	PickedMesh hoverMesh;  // last raycast hit under the cursor (updated per frame)
	float lastMouseX = -1.f;
	float lastMouseY = -1.f;
};
