#pragma once

#include <array>

/** @brief The project's external links, shared by the Home page and the header's help popover. */
namespace MenuLinks
{
	enum class Link
	{
		Wiki,
		DeveloperWiki,
		GitHub,
		NexusMods,
		Discord
	};

	/** @brief Every link, in the order the help popover lists them. */
	inline constexpr std::array kAll = { Link::Wiki, Link::DeveloperWiki, Link::GitHub, Link::NexusMods, Link::Discord };

	/** @brief The link's URL. */
	const char* GetUrl(Link link);

	/** @brief The link's translated label. */
	const char* GetLabel(Link link);

	/** @brief Opens the link in the default browser. */
	void Open(Link link);
}
