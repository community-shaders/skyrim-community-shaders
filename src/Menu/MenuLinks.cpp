#include "Menu/MenuLinks.h"

#include "I18n/I18n.h"

namespace MenuLinks
{
	const char* GetUrl(Link link)
	{
		switch (link) {
		case Link::Wiki:
			return "https://modding.wiki/en/skyrim/developers/community-shaders";
		case Link::DeveloperWiki:
			return "https://github.com/community-shaders/skyrim-community-shaders/wiki";
		case Link::GitHub:
			return "https://github.com/community-shaders/skyrim-community-shaders";
		case Link::NexusMods:
			return "https://www.nexusmods.com/skyrimspecialedition/mods/86492";
		case Link::Discord:
			return "https://discord.com/invite/nkrQybAsyy";
		}
		return "";
	}

	const char* GetLabel(Link link)
	{
		switch (link) {
		case Link::Wiki:
			return T("menu.home.wiki", "Wiki");
		case Link::DeveloperWiki:
			return T("menu.home.dev_wiki", "Developer Wiki");
		case Link::GitHub:
			return T("menu.home.github", "GitHub");
		case Link::NexusMods:
			return T("menu.home.nexus_mods", "Nexus Mods");
		case Link::Discord:
			return T("menu.help.discord", "Discord");
		}
		return "";
	}

	void Open(Link link)
	{
		ShellExecuteA(nullptr, "open", GetUrl(link), nullptr, nullptr, SW_SHOWNORMAL);
	}
}
