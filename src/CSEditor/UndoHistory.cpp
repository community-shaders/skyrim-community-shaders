#include "UndoHistory.h"

#include "Utils/UI.h"

namespace
{
	bool Differs(const UndoHistory::SnapshotPtr& a, const UndoHistory::SnapshotPtr& b)
	{
		return a && b && !a->Equals(*b);
	}
}

void UndoHistory::Watch(Target target)
{
	const int frame = ImGui::GetFrameCount();
	auto& entry = watched[target.key];
	if (entry.lastFrame < frame - 1 || !entry.committed)
		entry.committed = target.capture();
	entry.target = std::move(target);
	entry.lastFrame = frame;
}

void UndoHistory::Change(const Target& target, const std::function<void()>& change)
{
	const auto before = target.capture();
	change();
	const auto after = target.capture();
	if (Differs(before, after))
		Record(target, before);
	if (auto found = watched.find(target.key); found != watched.end())
		found->second.committed = after;
}

void UndoHistory::EndFrame()
{
	const int frame = ImGui::GetFrameCount();
	std::erase_if(watched, [frame](const auto& item) { return item.second.lastFrame < frame - 1; });
	if (!Util::InteractionEndedThisFrame())
		return;
	for (auto& [_, entry] : watched) {
		if (entry.lastFrame != frame)
			continue;
		auto now = entry.target.capture();
		if (Differs(entry.committed, now))
			Record(entry.target, entry.committed);
		entry.committed = std::move(now);
	}
}

void UndoHistory::Record(const Target& target, SnapshotPtr before)
{
	undoStack.push_back({ target, std::move(before) });
	if (undoStack.size() > kMaxEntries)
		undoStack.erase(undoStack.begin());
	redoStack.clear();
}

std::optional<std::string> UndoHistory::Step(std::vector<Entry>& from, std::vector<Entry>& to)
{
	while (!from.empty()) {
		auto entry = std::move(from.back());
		from.pop_back();
		auto current = entry.target.capture();
		if (!current)
			continue;  // Its target is gone; nothing to put back.
		entry.target.restore(*entry.state);
		if (auto found = watched.find(entry.target.key); found != watched.end())
			found->second.committed = entry.target.capture();
		to.push_back({ entry.target, std::move(current) });
		return entry.target.label;
	}
	return std::nullopt;
}

std::optional<std::string> UndoHistory::Undo()
{
	return Step(undoStack, redoStack);
}

std::optional<std::string> UndoHistory::Redo()
{
	return Step(redoStack, undoStack);
}

void UndoHistory::Forget(std::string_view keyPrefix)
{
	const auto matches = [keyPrefix](const std::string& key) { return key.starts_with(keyPrefix); };
	std::erase_if(undoStack, [&](const Entry& entry) { return matches(entry.target.key); });
	std::erase_if(redoStack, [&](const Entry& entry) { return matches(entry.target.key); });
	std::erase_if(watched, [&](const auto& item) { return matches(item.first); });
}
