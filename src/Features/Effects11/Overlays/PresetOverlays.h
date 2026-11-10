#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Features/Effects11/SettingManager.h"
#include "OverlayTargets.h"

namespace Overlays
{
	/** @brief One overlay's effect on a row, for tooltips: its first changed value. */
	struct Contribution
	{
		size_t overlay = 0;
		OverlayValue before;
		OverlayValue after;
	};

	/** @brief Overlay state of one editor row (a setting or shader parameter). */
	struct RowInfo
	{
		std::vector<Contribution> contributions;  ///< One per overlay, in apply order
		bool locked = false;                      ///< Edits to this row are not allowed with the current edit target
	};

	/** @brief Applies ENB Extender preset overlays to the Effects 11 stores in memory, and records edits into them. */
	class Manager
	{
	public:
		static Manager& GetSingleton();

		/** @brief Re-reads the active preset's list and overlay files and drops all stacks. Call before effects compile, so #define reads see them. */
		void LoadFiles();
		/** @brief Resolves every op against the freshly loaded stores, logs a summary, and applies the active overlays. */
		void OnStoresLoaded();
		/** @brief Restores originals, rebuilds the stacks from the active overlays and writes the live values. */
		void Reapply();
		/** @brief Writes the original values back and drops the stacks, so a reload starts from unoverlaid stores. */
		void RestoreBeforeReload();
		/** @brief True while overlay values are written into the stores. */
		bool IsApplied() const { return applied && !stacks.empty(); }

		/** @brief Writes the original values back for its lifetime, so saves never contain overlay values. */
		class RemovedScope
		{
		public:
			RemovedScope();
			~RemovedScope();
			RemovedScope(const RemovedScope&) = delete;
			RemovedScope& operator=(const RemovedScope&) = delete;

		private:
			bool restored = false;
		};

		/**
		 * @brief Applies active ops to a #define value read while preprocessing, and records the compiled value.
		 * @param a_type The define's declared type: "bool", "int" or "float".
		 */
		void TransformDefine(std::string_view a_section, std::string_view a_key, std::string_view a_type, std::string& a_value);
		/** @brief Forgets the recorded #define values; call before effects compile. */
		void ClearCompiledDefines() { compiledDefines.clear(); }

		const std::vector<Overlay>& GetOverlays() const { return overlays; }
		const std::vector<std::string>& GetActive() const { return list.active; }
		const Overlay* FindOverlay(std::string_view a_fileName) const;
		bool IsActive(std::string_view a_fileName) const { return ActivePosition(a_fileName) >= 0; }
		/** @brief Ops of an overlay that match nothing in Effects 11. */
		size_t UnmappedCount(size_t a_overlay) const { return a_overlay < unmapped.size() ? unmapped[a_overlay] : 0; }
		bool HasFolder() const { return hasFolder; }
		/** @brief File the last list, Amount or overlay write failed on; empty when none failed. */
		const std::string& GetFailedWriteFile() const { return failedWriteFile; }
		/** @brief Increments whenever the overlay list is reloaded or grows, so UI caches can refresh. */
		uint32_t GetGeneration() const { return generation; }

		/** @brief Turns an overlay on (switching off those it lists as incompatible) or off, saving Active. */
		void SetEnabled(std::string_view a_fileName, bool a_enabled);
		/** @brief Moves an active overlay a_delta places in the apply order, saving Active. */
		void Move(std::string_view a_fileName, int a_delta);
		/** @brief Sets an overlay's Amount and re-applies; call SaveAmount when the edit ends. */
		void SetAmount(std::string_view a_fileName, float a_amount);
		/** @brief Writes an overlay's Amount to its file; unsaved authored ops stay in memory until Save. */
		void SaveAmount(std::string_view a_fileName);

		/** @brief Overlay state of a shader parameter row; null when no active overlay changes it. */
		const RowInfo* GetShaderRow(const Effect& a_effect, size_t a_index) const;
		/** @brief Overlay state of a setting row; null when no active overlay changes it. */
		const RowInfo* GetSettingRow(uint32_t a_settingID) const;
		/** @brief True when the edit target's Amount is 0, so no edit can be recorded. */
		bool AllRowsLocked() const { return allRowsLocked; }
		/** @brief True while the active overlays give a #define a value other than the one compiled. */
		bool IsShaderReloadNeeded() const { return shaderReloadNeeded; }

		/** @brief Overlay that editor edits go to; empty for the base preset. */
		const std::string& GetEditTarget() const { return editTarget; }
		/** @brief Selects the edit target; a non-empty one is enabled first. */
		void SetEditTarget(std::string a_fileName);
		/** @brief Records a shader parameter edit into the edit target. @param a_before The value before the widget changed it. @return False when the base preset is the target. */
		bool TakeShaderEdit(Effect& a_effect, size_t a_index, const OverlayValue& a_before);
		/** @brief Records a setting edit into the edit target. @param a_weatherFile Weather file the edit belongs to, empty for enbseries.ini. @return False when the base preset is the target. */
		bool TakeSettingEdit(uint32_t a_settingID, const std::string& a_weatherFile, const SettingValue& a_before, const SettingValue& a_after);
		/** @brief Creates an empty overlay file, enables it and makes it the edit target. */
		bool CreateOverlay(std::string_view a_name);
		/** @brief Moves unsaved base edits into a new overlay: snapshots every value, runs a_revert to reload from disk, and records each difference. */
		bool CreateOverlayFromChanges(std::string_view a_name, const std::function<void()>& a_revert);
		/** @brief Whether any overlay has authored ops not yet written to its file. */
		bool HasDirtyOverlays() const;
		/** @brief Writes every overlay with unsaved authored ops. */
		bool SaveDirtyOverlays();

	private:
		Manager() = default;

		/** @brief One op's step in a stack: the overlay and the values around it. */
		struct StackOp
		{
			size_t overlay = 0;
			OverlayValue before;
			OverlayValue after;
		};
		/** @brief Every active op on one slot, with the slot's original and resulting value. */
		struct Stack
		{
			SlotValue original;
			SlotValue live;
			std::vector<StackOp> ops;
		};
		/** @brief A #define as read while preprocessing, and the value it was compiled with. */
		struct CompiledDefine
		{
			std::string section;
			std::string key;  ///< Normalized with Effect::NormalizeIniKey
			std::string type;
			std::string iniValue;
			std::string compiledValue;
		};

		/** @brief The enbpresetoverlays folder of the active preset. */
		std::filesystem::path FolderPath() const;
		/** @brief FindOverlay for edits; a separate name so callers holding a non-const Manager still reach the public const one. */
		Overlay* FindOverlayMutable(std::string_view a_fileName);
		/** @brief Index of an overlay in the Active order, or -1 when it is off. */
		int ActivePosition(std::string_view a_fileName) const;
		/** @brief Resolves one overlay's ops; incompatible or unmatched ops count as unmapped. */
		void ResolveOverlay(size_t a_overlay);
		/** @brief Resolves an op, dropping targets whose value the operand cannot change. */
		std::vector<ResolvedTarget> ResolveCompatible(const Op& a_op, std::string& a_reason) const;
		/** @brief Pushes one overlay's ops onto the stacks of the slots they target. */
		void ApplyOverlay(size_t a_overlay);
		/** @brief Writes every stack's original value; weather writes wait for FlushWeatherWrites. */
		void RestoreOriginals();
		/** @brief Writes every stack's live value; weather writes wait for FlushWeatherWrites. */
		void WriteLive();
		/** @brief Rebuilds the row markers and locks, and the all-rows lock, from the stacks and edit target. */
		void RebuildRowInfo();
		/** @brief The value a #define compiles to under the current active overlays. */
		std::string CompileDefine(const CompiledDefine& a_define) const;
		/** @brief Writes the Active order to the list file. */
		void SaveListFile();
		/** @brief Writes an overlay file and clears its dirty flag. */
		bool SaveOverlayFile(Overlay& a_overlay);
		/** @brief Adds, rewrites or removes the edit target's op for one slot so the slot ends at a_after. */
		void RecordEdit(const SlotKey& a_slot, const OverlayValue& a_before, const OverlayValue& a_after);

		std::filesystem::path seriesPath;
		std::vector<Overlay> overlays;
		OverlayList list;
		std::optional<Resolver> resolver;
		std::vector<std::vector<std::vector<ResolvedTarget>>> resolved;  ///< [overlay][op] -> targets
		std::vector<size_t> unmapped;
		std::map<SlotKey, Stack> stacks;
		std::map<std::pair<const Effect*, size_t>, RowInfo> shaderRows;
		std::unordered_map<uint32_t, RowInfo> settingRows;
		std::vector<CompiledDefine> compiledDefines;  ///< Every #define read since the last compile started
		std::string editTarget;
		std::string failedWriteFile;
		uint32_t generation = 0;
		bool applied = false;
		bool hasFolder = false;
		bool shaderReloadNeeded = false;
		bool allRowsLocked = false;
	};
}
