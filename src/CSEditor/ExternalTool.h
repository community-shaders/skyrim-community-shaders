#pragma once

#include <string>

/// A CS editor tool that writes into another mod's files, such as the Light editor writing Light Placer
/// configs. Its changes are written by its own button, never by the CS editor's save, so saving reminds
/// about unwritten changes and closing the editor asks before they are lost.
struct ExternalTool
{
	virtual ~ExternalTool() = default;

	/** @brief The mod the tool writes into, for prompts ("Light Placer"). */
	virtual const char* GetTargetName() const = 0;

	/** @brief What the unwritten changes belong to, for prompts (the selected light). */
	virtual std::string DescribeChanges() const = 0;

	/** @brief Whether the tool holds edits not yet written. */
	virtual bool HasUnwrittenChanges() const = 0;

	/** @brief Whether those edits can be written now; some can only ever be previewed. */
	virtual bool CanWriteChanges() const = 0;

	/** @brief Writes the edits and reports the result to the user. @return True on success. */
	virtual bool WriteChanges() = 0;

	/** @brief Drops the edits, putting back what was there before. */
	virtual void DiscardChanges() = 0;
};
