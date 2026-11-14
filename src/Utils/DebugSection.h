#pragma once

namespace Util
{
	/** @brief Hides feature Debug sections while alive (CS Editor Features window); restores the previous state. */
	class HideDebugSectionsGuard
	{
	public:
		explicit HideDebugSectionsGuard(bool hide);
		~HideDebugSectionsGuard();

		HideDebugSectionsGuard(const HideDebugSectionsGuard&) = delete;
		HideDebugSectionsGuard& operator=(const HideDebugSectionsGuard&) = delete;

	private:
		bool previous;
	};

	/** @brief False while a HideDebugSectionsGuard hides them; DebugSection checks this, so features need not. */
	bool ShowDebugSections();

	/**
	 * @brief Uniform "Debug" section for a feature's menu page.
	 *
	 * Features opt in by overriding Feature::HasDebugSection() and Feature::DrawDebugSection(); the
	 * feature page then draws the section after the settings (before Profiling) with the same heading
	 * style as the other page sections. Constructing the object draws the heading; the section body is
	 * only drawn while the object tests true, which is false when debug sections are hidden
	 * (see HideDebugSectionsGuard).
	 *
	 * @code
	 * void MyFeature::DrawDebugSection()
	 * {
	 *     Util::DebugSection section;
	 *     if (section) {
	 *         section.Hint(T(TKEY("debug_hint"), "Visualises intermediate buffers."));
	 *         ImGui::Checkbox(T(TKEY("show_buffers"), "Show Buffers"), &showBuffers);
	 *     }
	 * }
	 * @endcode
	 */
	class DebugSection
	{
	public:
		/** @brief Draws the "Debug" heading unless debug sections are hidden. */
		DebugSection();
		~DebugSection() = default;

		DebugSection(const DebugSection&) = delete;
		DebugSection& operator=(const DebugSection&) = delete;

		/** @brief Whether the section body should be drawn. */
		explicit operator bool() const { return visible; }

		/**
		 * @brief Draws a wrapped, dimmed description line at the top of the section.
		 * @param text Already-translated text.
		 */
		void Hint(const char* text) const;

	private:
		bool visible;
	};
}
