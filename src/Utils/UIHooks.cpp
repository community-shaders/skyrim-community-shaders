#include "Utils/UIHooks.h"

#include "Utils/UIAnim.h"

#include <detours/detours.h>
#include <imgui.h>
#include <imgui_internal.h>

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

	// Every popup and menu opens through BeginPopupEx, every combo through BeginComboPopup, and all end in EndPopup
	auto* RealBeginPopupEx = &ImGui::BeginPopupEx;
	auto* RealBeginComboPopup = &ImGui::BeginComboPopup;
	auto* RealEndPopup = &ImGui::EndPopup;

	bool DetouredBeginPopupEx(ImGuiID id, ImGuiWindowFlags flags)
	{
		return Util::Anim::FadedBeginPopup(RealBeginPopupEx, id, flags);
	}

	bool DetouredBeginComboPopup(ImGuiID id, const ImRect& bb, ImGuiComboFlags flags)
	{
		return Util::Anim::FadedBeginComboPopup(RealBeginComboPopup, id, bb, flags);
	}

	void DetouredEndPopup()
	{
		Util::Anim::FadedEndPopup(RealEndPopup);
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
		const std::pair<PVOID*, PVOID> hooks[] = {
			{ reinterpret_cast<PVOID*>(&RealSelectable), reinterpret_cast<PVOID>(&DetouredSelectable) },
			{ reinterpret_cast<PVOID*>(&RealBeginPopupEx), reinterpret_cast<PVOID>(&DetouredBeginPopupEx) },
			{ reinterpret_cast<PVOID*>(&RealBeginComboPopup), reinterpret_cast<PVOID>(&DetouredBeginComboPopup) },
			{ reinterpret_cast<PVOID*>(&RealEndPopup), reinterpret_cast<PVOID>(&DetouredEndPopup) },
		};
		for (const auto& [target, detour] : hooks) {
			if (const auto result = DetourAttach(target, detour); result != NO_ERROR) {
				DetourTransactionAbort();
				logger::error("UIHooks: DetourAttach failed ({}); lists and popups keep ImGui's own look", result);
				return false;
			}
		}
		if (const auto result = DetourTransactionCommit(); result != NO_ERROR) {
			logger::error("UIHooks: DetourTransactionCommit failed ({}); lists and popups keep ImGui's own look", result);
			return false;
		}

		installed = true;
		logger::info("UIHooks: installed");
		return true;
	}
}
