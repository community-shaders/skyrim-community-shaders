#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Effect;

namespace UITree
{
	/** @brief How the editor wants shader parameters drawn, and what happened while drawing them. */
	struct ViewOptions
	{
		std::string_view filter;                             ///< Case-insensitive name filter; empty shows every parameter
		std::function<bool(const std::string&)> showPeriod;  ///< Whether parameters of a time period ("Dawn", "Interior", ...) are shown
		int drawn = 0;                                       ///< out: parameters drawn this call
		bool changed = false;                                ///< out: a value or technique changed
		bool compileTimeChanged = false;                     ///< out: a #define-backed value changed, which needs a shader reload
	};

	enum class FilterMode
	{
		All,
		TopLevelOnly,
		NonTopLevelOnly
	};

	struct VarRef
	{
		Effect* effect = nullptr;
		int index = -1;
	};

	struct GroupMeta
	{
		std::string displayName;
		int ordering = 0;
		bool defaultOpen = false;
		bool hasOrdering = false;
		bool isTopLevel = false;
	};

	using MetaMap = std::unordered_map<std::string, GroupMeta>;

	struct GroupNode;

	struct Item
	{
		enum class Type
		{
			Variable,
			Separator,
			Group
		};
		Type type = Type::Variable;
		int ordering = 0;
		int sourceOrder = INT_MAX;
		bool hasOrdering = false;
		VarRef var;
		std::unique_ptr<GroupNode> group;
	};

	struct GroupNode
	{
		std::string name;
		std::string fullPath;
		std::vector<Item> items;

		GroupNode* FindOrCreateChild(const std::string& segment, const std::string& fullPath);
	};

	struct Tree
	{
		GroupNode root;
		MetaMap meta;
		std::unordered_map<std::string, VarRef> uniqueNameMap;
		std::unordered_map<std::string, std::unordered_map<std::string, VarRef>> fileUniqueNameMap;

		void Build(std::span<Effect*> effects, FilterMode filter = FilterMode::All);
		void Sort();
	};

	GroupNode* TraverseGroupPath(GroupNode& root, const std::string& groupPath, const MetaMap& meta = {});
	int ComputeMinSourceOrder(const GroupNode& node);
}
