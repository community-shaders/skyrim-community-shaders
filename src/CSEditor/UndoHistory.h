#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * @brief The CS editor's undo and redo: one history for every kind of editing (widgets, feature
 *        settings, Scene Manager, the Light editor).
 *
 * Each editable thing registers itself as a Target while its UI is drawn. When an interaction ends
 * (a slider let go, a button clicked, a text field left), each target drawn that frame is compared
 * with its state before the interaction; one that changed records that earlier state. So one drag
 * is one step, and a button that rewrites settings is undoable like any control.
 */
class UndoHistory
{
public:
	/** @brief A target's state at one point, compared to tell whether an interaction changed it. */
	struct Snapshot
	{
		virtual ~Snapshot() = default;
		virtual bool Equals(const Snapshot& other) const = 0;
	};
	using SnapshotPtr = std::shared_ptr<const Snapshot>;

	/** @brief A snapshot holding a copy of a value with operator==. */
	template <class T>
	struct Value final : Snapshot
	{
		T value;
		explicit Value(T a_value) :
			value(std::move(a_value)) {}
		bool Equals(const Snapshot& other) const override { return value == static_cast<const Value&>(other).value; }
	};

	/** @brief Wraps a copy of value as a snapshot. */
	template <class T>
	static SnapshotPtr Capture(T value)
	{
		return std::make_shared<const Value<T>>(std::move(value));
	}

	/** @brief The value inside a snapshot made by Capture<T>. */
	template <class T>
	static const T& Get(const Snapshot& snapshot)
	{
		return static_cast<const Value<T>&>(snapshot).value;
	}

	/** @brief Something the history can step back. Its functions must resolve the thing from stable
	 *         identities rather than hold pointers that may not outlive the entry. */
	struct Target
	{
		/// Identity; every entry for the same thing shares it.
		std::string key;
		/// Names the thing in the undo and redo notifications.
		std::string label;
		/// The current state, or null when the thing is gone; an entry whose target is gone is dropped.
		std::function<SnapshotPtr()> capture;
		/// Puts a captured state back.
		std::function<void(const Snapshot&)> restore;
	};

	/**
	 * @brief Registers a target whose UI is about to be drawn this frame. Call before drawing it: a
	 *        target first seen, or seen again after a frame away, records its current state as the
	 *        starting point for the next change.
	 */
	void Watch(Target target);

	/**
	 * @brief Records any change to an unwatched or watched target made by a single action outside a
	 *        drawn control, such as a shortcut or a menu command.
	 * @param target The thing the action changes.
	 * @param change Makes the change.
	 */
	void Change(const Target& target, const std::function<void()>& change);

	/** @brief Call once per frame after every target is drawn; records the targets an interaction changed. */
	void EndFrame();

	/** @brief Steps the newest change back. @return Its target's label, or nullopt if there was nothing to undo. */
	std::optional<std::string> Undo();
	/** @brief Reapplies the newest undone change. @return Its target's label, or nullopt if there was nothing to redo. */
	std::optional<std::string> Redo();

	/** @brief Whether there is a change to undo. */
	bool CanUndo() const { return !undoStack.empty(); }
	/** @brief Whether there is an undone change to redo. */
	bool CanRedo() const { return !redoStack.empty(); }
	/** @brief Number of changes that can be undone. */
	size_t GetUndoCount() const { return undoStack.size(); }
	/** @brief Number of undone changes that can be redone. */
	size_t GetRedoCount() const { return redoStack.size(); }

	/** @brief Drops every entry and starting point for targets whose key starts with keyPrefix. */
	void Forget(std::string_view keyPrefix);

private:
	struct Entry
	{
		Target target;
		SnapshotPtr state;
	};
	struct Watched
	{
		Target target;
		SnapshotPtr committed;
		int lastFrame = -1;
	};

	static constexpr size_t kMaxEntries = 100;

	std::unordered_map<std::string, Watched> watched;
	std::vector<Entry> undoStack;
	std::vector<Entry> redoStack;

	/** @brief Pushes a change onto the undo stack, clearing redo, which it invalidates. */
	void Record(const Target& target, SnapshotPtr before);
	/** @brief Pops from one stack, restores, and pushes the replaced state onto the other. */
	std::optional<std::string> Step(std::vector<Entry>& from, std::vector<Entry>& to);
};
