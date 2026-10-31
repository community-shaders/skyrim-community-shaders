#include "PresetCompatibility.h"

#include "Feature.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Plugin.h"
#include "Utils/Format.h"

#include <cctype>
#include <charconv>
#include <format>
#include <ranges>
#include <system_error>

namespace PresetCompatibility
{
	namespace
	{
		bool ParseComponent(std::string_view text, size_t& cursor, int& out)
		{
			if (cursor >= text.size() || !std::isdigit(static_cast<unsigned char>(text[cursor])))
				return false;
			const size_t start = cursor;
			while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor])))
				++cursor;
			const auto slice = text.substr(start, cursor - start);
			const auto* begin = slice.data();
			const auto* end = begin + slice.size();
			int value = 0;
			const auto result = std::from_chars(begin, end, value);
			if (result.ec != std::errc{} || result.ptr != end)
				return false;
			out = value;
			return true;
		}

		bool ConsumeDot(std::string_view text, size_t& cursor)
		{
			if (cursor >= text.size() || text[cursor] != '.')
				return false;
			++cursor;
			return true;
		}

		std::string JoinNames(const std::vector<std::string>& names)
		{
			return names | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>();
		}
	}

	bool ParseSemVer(std::string_view text, SemVer& out)
	{
		while (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
			text.remove_prefix(1);
		size_t cursor = 0;
		SemVer parsed{};
		// A trailing build segment (e.g. 0.8.0.0) is tolerated by ignoring the rest.
		if (!ParseComponent(text, cursor, parsed.major) || !ConsumeDot(text, cursor) ||
			!ParseComponent(text, cursor, parsed.minor) || !ConsumeDot(text, cursor) ||
			!ParseComponent(text, cursor, parsed.patch))
			return false;
		out = parsed;
		return true;
	}

	Feature::ReleaseStage ReleaseStageFromVersion(std::string_view version)
	{
		using enum Feature::ReleaseStage;
		SemVer parsed{};
		if (!ParseSemVer(version, parsed))
			return Release;
		if (parsed.major > 0)
			return Release;
		if (parsed.minor > 0)
			return Beta;
		return Alpha;
	}

	VersionGap CompareRequiredCsVersion(std::string_view requiredCsVersion)
	{
		if (requiredCsVersion.empty())
			return VersionGap::None;

		SemVer required{};
		if (!ParseSemVer(requiredCsVersion, required))
			return VersionGap::None;

		const SemVer current{
			.major = static_cast<int>(Plugin::VERSION.major()),
			.minor = static_cast<int>(Plugin::VERSION.minor()),
			.patch = static_cast<int>(Plugin::VERSION.patch()),
		};

		if (current.major != required.major)
			return current.major < required.major ? VersionGap::Major : VersionGap::None;
		if (current.minor != required.minor)
			return current.minor < required.minor ? VersionGap::Minor : VersionGap::None;
		// Patch-only lag is intentionally silent.
		return VersionGap::None;
	}

	std::vector<std::string> MissingRequiredFeatures(const std::vector<std::string>& requiredFeatures)
	{
		std::vector<std::string> missing;
		for (const auto& shortName : requiredFeatures) {
			if (shortName.empty())
				continue;
			if (!Feature::FindFeatureByShortName(shortName))
				missing.push_back(shortName);
		}
		return missing;
	}

	std::vector<std::string> MissingRequiredPlugins(const std::vector<std::string>& requiredPlugins)
	{
		std::vector<std::string> missing;
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return missing;
		for (const auto& plugin : requiredPlugins) {
			if (!plugin.empty() && !dataHandler->LookupLoadedModByName(plugin) && !dataHandler->LookupLoadedLightModByName(plugin))
				missing.push_back(plugin);
		}
		return missing;
	}

	std::string CurrentCsVersionString()
	{
		return Util::GetFormattedVersion(Plugin::VERSION);
	}

	Warning Evaluate(std::string_view requiredCsVersion, const std::vector<std::string>& requiredFeatures,
		const std::vector<std::string>& requiredPlugins)
	{
		Warning warning;
		warning.versionGap = CompareRequiredCsVersion(requiredCsVersion);
		warning.missingFeatures = MissingRequiredFeatures(requiredFeatures);
		warning.missingPlugins = MissingRequiredPlugins(requiredPlugins);

		auto* i18n = I18n::GetSingleton();
		const std::string required{ requiredCsVersion };
		if (warning.versionGap == VersionGap::Major) {
			warning.versionMessage = i18n->Format("menu.presets.compat_cs_major",
				{ { "required", required }, { "current", CurrentCsVersionString() } },
				"Made for Community Shaders {required}. This build ({current}) is a major version behind and may not work correctly.");
			warning.versionMessageCompact = i18n->Format("menu.presets.compat_cs_major_short",
				{ { "required", required } },
				"Needs Community Shaders {required}+");
		} else if (warning.versionGap == VersionGap::Minor) {
			warning.versionMessage = i18n->Format("menu.presets.compat_cs_minor",
				{ { "required", required }, { "current", CurrentCsVersionString() } },
				"Made for Community Shaders {required}. This build ({current}) is slightly older.");
			warning.versionMessageCompact = i18n->Format("menu.presets.compat_cs_minor_short",
				{ { "required", required } },
				"Made for CS {required}");
		}

		if (!warning.missingFeatures.empty()) {
			warning.featuresMessage = i18n->Format("menu.presets.compat_features_missing",
				{ { "features", JoinNames(warning.missingFeatures) } },
				"Missing required feature(s): {features}");
		}
		if (!warning.missingPlugins.empty()) {
			warning.pluginsMessage = i18n->Format("menu.presets.compat_plugins_missing",
				{ { "plugins", JoinNames(warning.missingPlugins) } },
				"Made with plugin(s) not in your load order: {plugins}");
		}
		return warning;
	}

	bool DrawWarnings(const Warning& warning, bool compact)
	{
		const auto& theme = Menu::GetSingleton()->GetTheme();
		bool drew = false;

		const auto drawLine = [&](const ImVec4& color, const std::string& text) {
			if (text.empty())
				return;
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (compact)
				ImGui::TextUnformatted(text.c_str());
			else
				ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
			drew = true;
		};

		const auto& versionText = compact && !warning.versionMessageCompact.empty() ?
		                              warning.versionMessageCompact :
		                              warning.versionMessage;

		if (warning.versionGap == VersionGap::Major)
			drawLine(theme.StatusPalette.Error, versionText);
		else if (warning.versionGap == VersionGap::Minor)
			drawLine(theme.StatusPalette.Warning, versionText);

		drawLine(theme.StatusPalette.Warning, warning.featuresMessage);
		drawLine(theme.StatusPalette.Warning, warning.pluginsMessage);
		return drew;
	}
}
