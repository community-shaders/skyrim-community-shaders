#include "UITree.h"

#include "Effects/Effect.h"

namespace UITree
{
	GroupNode* GroupNode::FindOrCreateChild(const std::string& segment, const std::string& a_fullPath)
	{
		for (auto& item : items) {
			if (item.type == Item::Type::Group && item.group && item.group->name == segment)
				return item.group.get();
		}
		auto node = std::make_unique<GroupNode>();
		node->name = segment;
		node->fullPath = a_fullPath;
		Item item;
		item.type = Item::Type::Group;
		item.group = std::move(node);
		auto* ptr = item.group.get();
		items.push_back(std::move(item));
		return ptr;
	}

	GroupNode* TraverseGroupPath(GroupNode& root, const std::string& groupPath, const MetaMap& meta)
	{
		GroupNode* node = &root;
		size_t start = 0;
		while (start < groupPath.size()) {
			size_t dot = groupPath.find('.', start);
			if (dot == std::string::npos)
				dot = groupPath.size();
			std::string fullPath = groupPath.substr(0, dot);
			auto metaIt = meta.find(fullPath);
			if (metaIt != meta.end() && metaIt->second.isTopLevel)
				node = &root;
			node = node->FindOrCreateChild(groupPath.substr(start, dot - start), fullPath);
			start = dot + 1;
		}
		return node;
	}

	int ComputeMinSourceOrder(const GroupNode& node)
	{
		int minSO = INT_MAX;
		for (auto& item : node.items) {
			if (item.type == Item::Type::Group && item.group) {
				int cso = ComputeMinSourceOrder(*item.group);
				if (cso < minSO)
					minSO = cso;
			} else {
				if (item.sourceOrder < minSO)
					minSO = item.sourceOrder;
			}
		}
		return minSO;
	}

	void Tree::Build(std::span<Effect*> effects, FilterMode filter)
	{
		std::unordered_set<std::string> seenItems;

		constexpr int sourceOrderOffset = 100000;
		int effectIndex = 0;

		for (auto* effect : effects) {
			if (!effect->IsCompiled())
				continue;

			int offset = effectIndex++ * sourceOrderOffset;

			for (auto& [path, gm] : effect->groupMeta) {
				auto [it, inserted] = meta.try_emplace(path, GroupMeta{});
				if (inserted) {
					it->second.displayName = gm.displayName;
					it->second.defaultOpen = gm.defaultOpen;
					it->second.ordering = gm.ordering;
					it->second.hasOrdering = gm.hasOrdering;
					it->second.isTopLevel = gm.isTopLevel;
				} else {
					if (it->second.displayName.empty() && !gm.displayName.empty())
						it->second.displayName = gm.displayName;
					if (!it->second.defaultOpen && gm.defaultOpen)
						it->second.defaultOpen = gm.defaultOpen;
					if (!it->second.hasOrdering && gm.hasOrdering) {
						it->second.ordering = gm.ordering;
						it->second.hasOrdering = true;
					}
					if (!it->second.isTopLevel && gm.isTopLevel)
						it->second.isTopLevel = true;
				}
			}

			auto& fileMap = fileUniqueNameMap[effect->GetName()];

			for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
				auto& var = effect->uiVariables[i];

				std::string uname = !var.uniqueName.empty() ? var.uniqueName
				                    : !var.group.empty()    ? var.group + "." + var.displayName
				                                            : var.displayName;
				uniqueNameMap[uname] = { effect, i };
				fileMap[uname] = { effect, i };

				if (filter == FilterMode::TopLevelOnly && !var.isTopLevel)
					continue;
				if (filter == FilterMode::NonTopLevelOnly && var.isTopLevel)
					continue;

				if (!seenItems.insert(var.name).second)
					continue;

				GroupNode* node = !var.group.empty() ? TraverseGroupPath(root, var.group, meta) : &root;

				Item item;
				item.type = Item::Type::Variable;
				item.var = { effect, i };
				item.ordering = var.ordering;
				item.sourceOrder = var.sourceOrder + offset;
				node->items.push_back(std::move(item));
			}

			for (auto& sep : effect->separators) {
				if (filter == FilterMode::TopLevelOnly && !sep.isTopLevel)
					continue;
				if (filter == FilterMode::NonTopLevelOnly && sep.isTopLevel)
					continue;

				if (!sep.name.empty() && !seenItems.insert(sep.name).second)
					continue;

				GroupNode* node = sep.isTopLevel ? &root
				                 : !sep.group.empty() ? TraverseGroupPath(root, sep.group, meta)
				                                      : &root;

				Item item;
				item.type = Item::Type::Separator;
				item.sourceOrder = sep.sourceOrder + offset;
				item.ordering = sep.ordering;
				item.hasOrdering = sep.hasOrdering;
				node->items.push_back(std::move(item));
			}
		}
	}

	static void InheritChildOrdering(GroupNode& node, MetaMap& meta)
	{
		for (auto& item : node.items) {
			if (item.type != Item::Type::Group || !item.group)
				continue;
			InheritChildOrdering(*item.group, meta);
			auto it = meta.find(item.group->fullPath);
			if (it == meta.end() || it->second.hasOrdering)
				continue;
			int maxChildOrder = 0;
			for (auto& childItem : item.group->items) {
				if (childItem.type == Item::Type::Group && childItem.group) {
					auto gcIt = meta.find(childItem.group->fullPath);
					if (gcIt != meta.end() && gcIt->second.ordering > maxChildOrder)
						maxChildOrder = gcIt->second.ordering;
				}
			}
			if (maxChildOrder > 0) {
				it->second.ordering = maxChildOrder;
				it->second.hasOrdering = true;
			}
		}
	}

	static void SortNode(GroupNode& node, const MetaMap& meta)
	{
		for (auto& item : node.items) {
			if (item.type == Item::Type::Group && item.group) {
				auto metaIt = meta.find(item.group->fullPath);
				if (metaIt != meta.end())
					item.ordering = metaIt->second.ordering;
				item.sourceOrder = ComputeMinSourceOrder(*item.group);
			}
		}

		std::stable_sort(node.items.begin(), node.items.end(), [](const Item& a, const Item& b) {
			return a.sourceOrder < b.sourceOrder;
		});

		for (size_t i = 0; i < node.items.size(); ++i) {
			if (node.items[i].type != Item::Type::Separator || node.items[i].hasOrdering)
				continue;
			for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
				if (node.items[j].type != Item::Type::Separator) {
					node.items[i].ordering = node.items[j].ordering;
					break;
				}
			}
		}

		std::stable_sort(node.items.begin(), node.items.end(), [](const Item& a, const Item& b) {
			if (a.ordering != b.ordering)
				return a.ordering > b.ordering;
			return a.sourceOrder < b.sourceOrder;
		});

		for (auto& item : node.items) {
			if (item.type == Item::Type::Group && item.group)
				SortNode(*item.group, meta);
		}
	}

	void Tree::Sort()
	{
		InheritChildOrdering(root, meta);
		SortNode(root, meta);
	}
}
