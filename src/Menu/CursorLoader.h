#pragma once

class Menu;

namespace Util::CursorLoader
{
	void MigrateLegacyCursorSettings(Menu::ThemeSettings& theme);
	bool Reload(Menu* menu);
	int GetLoadedCount();
	void Shutdown();
	void DrawCustomCursor(const Menu& menu);
}
