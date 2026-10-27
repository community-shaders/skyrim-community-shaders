#include "Utils/UIHooks.h"

#include "Utils/UIAnim.h"

#include <detours/detours.h>
#include <imgui.h>

namespace
{
	bool installed = false;

	// DetourAttach repoints this at a trampoline into the real ImGui function. The pointer-to-bool overload of
	// Selectable calls this one, so hooking it covers both, and ImGui's own callers (combos, menus, list boxes).
	auto* RealSelectable = static_cast<bool (*)(const char*, bool, ImGuiSelectableFlags, const ImVec2&)>(&ImGui::Selectable);

	bool DetouredSelectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size)
	{
		return Util::Anim::GlideSelectable(RealSelectable, label, selected, flags, size);
	}
}

namespace Util::UIHooks
{
	bool Install()
	{
		if (installed)
			return true;

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto result = DetourAttach(reinterpret_cast<PVOID*>(&RealSelectable), reinterpret_cast<PVOID>(&DetouredSelectable)); result != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("UIHooks: DetourAttach failed for ImGui::Selectable ({}); lists keep ImGui's own highlight", result);
			return false;
		}
		if (const auto result = DetourTransactionCommit(); result != NO_ERROR) {
			logger::error("UIHooks: DetourTransactionCommit failed ({}); lists keep ImGui's own highlight", result);
			return false;
		}

		installed = true;
		logger::info("UIHooks: installed");
		return true;
	}
}
