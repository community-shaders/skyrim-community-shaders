#include "PCH.h"

#include "CursorLoader.h"
#include "Menu.h"

namespace Util::CursorLoader
{
	namespace
	{
		struct LoadedCursor
		{
			ID3D11ShaderResourceView* texture = nullptr;
			ImVec2 size{};
			ImVec2 hotspot{};

			void Release()
			{
				if (texture) {
					texture->Release();
					texture = nullptr;
				}
				size = {};
				hotspot = {};
			}
		};

		eastl::array<LoadedCursor, ImGuiMouseCursor_COUNT> g_cursors = {};

		void ForEachSlot(const Menu::ThemeSettings& theme, auto&& fn)
		{
			static constexpr struct { ImGuiMouseCursor cursor; const char* defaultFile; } kSlots[] = {
				{ ImGuiMouseCursor_Arrow, "cursor.png" },
				{ ImGuiMouseCursor_TextInput, "cursor_text.png" },
				{ ImGuiMouseCursor_ResizeAll, "cursor_resize_all.png" },
				{ ImGuiMouseCursor_ResizeNS, "cursor_resize_ns.png" },
				{ ImGuiMouseCursor_ResizeEW, "cursor_resize_ew.png" },
				{ ImGuiMouseCursor_ResizeNESW, "cursor_resize_nesw.png" },
				{ ImGuiMouseCursor_ResizeNWSE, "cursor_resize_nwse.png" },
				{ ImGuiMouseCursor_Hand, "cursor_hand.png" },
				{ ImGuiMouseCursor_NotAllowed, "cursor_not_allowed.png" },
			};
			for (const auto& slot : kSlots) {
				fn(slot.cursor, slot.defaultFile, theme.Cursor.Types[static_cast<size_t>(slot.cursor)]);
			}
		}

		std::string EffectiveFile(const Menu::ThemeSettings::CursorImageSettings& settings, const char* defaultFile)
		{
			return !settings.File.empty() ? settings.File : defaultFile;
		}

		std::filesystem::path ResolvePath(const Menu& menu, const std::string& fileName)
		{
			if (fileName.empty()) {
				return {};
			}
			const auto& preset = menu.GetSettings().SelectedThemePreset;
			if (!preset.empty()) {
				auto themePath = Util::PathHelpers::GetThemesPath() / preset / fileName;
				if (std::filesystem::exists(themePath)) {
					return themePath;
				}
			}
			auto sharedPath = Util::PathHelpers::GetCursorsPath() / fileName;
			return std::filesystem::exists(sharedPath) ? sharedPath : std::filesystem::path{};
		}

		bool IsPathAllowed(const std::filesystem::path& path)
		{
			return Util::IsPathWithinDirectory(Util::PathHelpers::GetThemesPath(), path) ||
				   Util::IsPathWithinDirectory(Util::PathHelpers::GetCursorsPath(), path);
		}
	}

	void MigrateLegacyCursorSettings(Menu::ThemeSettings& theme)
	{
		auto& types = theme.Cursor.Types;
		auto& arrow = types[ImGuiMouseCursor_Arrow];
		if (arrow.File.empty() && !theme.Cursor.File.empty()) {
			arrow.File = theme.Cursor.File;
			arrow.HotspotX = theme.Cursor.HotspotX;
			arrow.HotspotY = theme.Cursor.HotspotY;
		}
	}

	int GetLoadedCount()
	{
		int count = 0;
		for (const auto& cursor : g_cursors) {
			if (cursor.texture) {
				++count;
			}
		}
		return count;
	}

	void Shutdown()
	{
		for (auto& cursor : g_cursors) {
			cursor.Release();
		}
	}

	bool Reload(Menu* menu)
	{
		if (!menu) {
			return false;
		}

		Shutdown();

		if (!menu->GetSettings().Theme.UseCustomCursor) {
			return true;
		}

		auto* device = globals::d3d::device;
		static bool loggedMissingDevice = false;
		if (!device) {
			if (!loggedMissingDevice) {
				logger::warn("CursorLoader::Reload: D3D device is null; will retry when available");
				loggedMissingDevice = true;
			}
			return false;
		}
		loggedMissingDevice = false;

		MigrateLegacyCursorSettings(menu->GetSettings().Theme);
		const auto& theme = menu->GetSettings().Theme;

		int loadedCount = 0;
		int failedCount = 0;
		ForEachSlot(theme, [&](ImGuiMouseCursor cursor, const char* defaultFile, const Menu::ThemeSettings::CursorImageSettings& settings) {
			const auto fileName = EffectiveFile(settings, defaultFile);
			const auto path = ResolvePath(*menu, fileName);
			if (path.empty() || !IsPathAllowed(path)) {
				return;
			}

			ID3D11ShaderResourceView* srv = nullptr;
			ImVec2 size{};
			if (!Util::LoadTextureFromFile(device, path.string().c_str(), &srv, size)) {
				++failedCount;
				return;
			}

			auto& loaded = g_cursors[static_cast<size_t>(cursor)];
			loaded.texture = srv;
			loaded.size = size;
			loaded.hotspot = ImVec2(settings.HotspotX, settings.HotspotY);
			++loadedCount;
		});

		if (loadedCount == 0) {
			logger::warn("CursorLoader::Reload: No cursor images found under Themes/<preset>/ or Interface/CommunityShaders/Cursors/");
		} else {
			if (failedCount > 0) {
				logger::warn("CursorLoader::Reload: Loaded {} custom cursor image(s); {} file(s) failed to decode", loadedCount, failedCount);
			} else {
				logger::info("CursorLoader::Reload: Loaded {} custom cursor image(s)", loadedCount);
			}
		}
		return true;
	}

	void DrawCustomCursor(const Menu& menu)
	{
		const auto& theme = menu.GetSettings().Theme;
		if (!theme.UseCustomCursor) {
			return;
		}

		auto& io = ImGui::GetIO();
		if (!io.MouseDrawCursor) {
			return;
		}

		const auto active = ImGui::GetMouseCursor();
		if (active <= ImGuiMouseCursor_None || active >= ImGuiMouseCursor_COUNT) {
			return;
		}

		const auto& loaded = g_cursors[static_cast<size_t>(active)];
		if (!loaded.texture) {
			return;
		}

		ImGui::SetMouseCursor(ImGuiMouseCursor_None);

		const float scale = (theme.Cursor.Scale > 0.0f ? theme.Cursor.Scale : 1.0f) * ImGui::GetStyle().MouseCursorScale;
		const ImVec2 drawSize{ loaded.size.x * scale, loaded.size.y * scale };
		const ImVec2 hotspot{ loaded.hotspot.x * scale, loaded.hotspot.y * scale };
		const ImVec2 pos{ io.MousePos.x - hotspot.x, io.MousePos.y - hotspot.y };

		ImGui::GetForegroundDrawList()->AddImage(
			reinterpret_cast<ImTextureID>(loaded.texture),
			pos,
			{ pos.x + drawSize.x, pos.y + drawSize.y },
			{},
			{ 1.0f, 1.0f },
			IM_COL32_WHITE);
	}
}
