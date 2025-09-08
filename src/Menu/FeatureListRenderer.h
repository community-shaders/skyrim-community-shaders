#pragma once

#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct Feature;

class FeatureListRenderer
{
public:
	struct BuiltInMenu
	{
		std::string name;
		std::function<void()> func;
	};

	struct CategoryHeader
	{
		std::string name;
	};

	using MenuFuncInfo = std::variant<BuiltInMenu, std::string, CategoryHeader, Feature*>;

	static void RenderFeatureList(
		float footerHeight,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::string& pendingFeatureSelection,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

private:
	struct ListMenuVisitor
	{
		size_t listId;
		size_t& selectedMenuRef;
		std::map<std::string, bool>& categoryExpansionStates;

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string& label);
		void operator()(const CategoryHeader& header);
		void operator()(Feature* feat);
	};

	struct DrawMenuVisitor
	{
		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string&);
		void operator()(const CategoryHeader&);
		void operator()(Feature* feat);

	private:
		// Helper methods for Feature rendering
		static bool IsFeatureInstalled(const std::string& featureName);
		static void RenderFeatureSettingsTab(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage);
		static void RenderFeatureAboutTab(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage);
		static void RenderFeatureActionButtons(Feature* feat, bool isDisabled, bool isLoaded, float buttonPadding, float buttonSpacing);
	};

	static std::vector<MenuFuncInfo> BuildMenuList(
		const std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

	static void HandlePendingFeatureSelection(
		std::string& pendingFeatureSelection,
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu);

	static void RenderLeftColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates);

	static void RenderRightColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t selectedMenu);
};