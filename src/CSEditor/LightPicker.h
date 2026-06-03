#pragma once

#include "RE/B/BSCoreTypes.h"      // RE::FormID
#include "RE/B/BSPointerHandle.h"  // RE::ObjectRefHandle

#include <string>

namespace RE
{
	class NiCamera;
}

// Resolves the mesh/reference under the cursor via a camera-through-cursor havok raycast.
// Isolated from LightEditor so the picking logic can be reasoned about and tested on its own.
struct LightPicker
{
	struct PickedMesh
	{
		RE::ObjectRefHandle refrHandle;          // safe across cell changes
		RE::FormID          baseFormId = 0;      // base object FormID
		std::string        editorId;         // base object EditorID (may be empty)
		std::string        modelPath;        // base object .nif path (may be empty)
		std::string        sourcePlugin;     // plugin defining the base FormID (may be empty)
		bool               valid = false;
	};

	enum class PickMode { kCollision = 0, kEffect = 1 };
	PickMode pickMode = PickMode::kCollision;   // persists across picks

	// Enters pick mode. While active, Update() watches for a world click.
	void BeginPick();
	// Leaves pick mode without producing a result.
	void Cancel();
	[[nodiscard]] bool IsPicking() const { return picking; }

	// Called once per frame while the editor is active. Performs the raycast on a
	// qualifying left-click and stores a result; handles right-click / ESC cancel.
	void Update();

	// Returns and clears the last successful pick (valid==true at most once per pick).
	[[nodiscard]] PickedMesh TakeResult();

private:
	static RE::NiCamera* GetPlayerNiCamera();
	static PickedMesh ResolveUnderCursor(bool logResult = true);
	static PickedMesh ResolveNearestToCursor();

	bool       picking = false;
	PickedMesh result;
	PickedMesh hoverMesh;           // last raycast hit under the cursor (updated per frame)
	float      lastMouseX = -1.f;
	float      lastMouseY = -1.f;
};
