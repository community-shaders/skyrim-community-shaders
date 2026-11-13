#pragma once

/**
 * @brief Always-on detours of ImGui widgets that give the whole UI shared behaviour without any widget opting in.
 * The scene interceptor hooks widgets for scene authoring; these are for polish.
 */
namespace Util::UIHooks
{
	/**
	 * @brief Hooks ImGui::Selectable so every list row gets the gliding highlight (Anim::GlideSelectable), and
	 * ImGui::BeginPopupEx / BeginComboPopup / EndPopup so every popup, menu and combo fades (Anim::FadedBeginPopup). Call once,
	 * on the render thread, before any UI draws; logs and leaves ImGui untouched if the hook cannot be placed.
	 * @return True when installed.
	 */
	bool Install();
}
