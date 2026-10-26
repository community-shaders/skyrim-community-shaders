#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsManager.h"

/// Binds one intercepted ImGui control to a scene entry and owns its gutter toggle.
namespace SceneWidgetBinding
{
	/// The caller's storage, erased to the primitive kinds the catalog can persist.
	struct Value
	{
		/// Widest control the interceptor covers: ColorEdit4 and float4 aggregates.
		static constexpr std::uint8_t kMaxComponents = 4;

		/** @brief Storage shape; Scalar carries any other ImGuiDataType through scalarType. */
		enum class Kind : std::uint8_t
		{
			Bool,
			Int,
			Float,
			FloatVector,
			Scalar
		};

		Kind kind = Kind::Float;
		void* data = nullptr;
		std::uint8_t componentCount = 1;
		ImGuiDataType scalarType = ImGuiDataType_COUNT;

		/** @brief Typed constructors, one per Kind. */
		static Value Bool(bool* a_data) { return { Kind::Bool, a_data, 1, ImGuiDataType_COUNT }; }
		static Value Int(int* a_data) { return { Kind::Int, a_data, 1, ImGuiDataType_COUNT }; }
		static Value Float(float* a_data) { return { Kind::Float, a_data, 1, ImGuiDataType_COUNT }; }
		static Value FloatVector(float* a_data, std::uint8_t a_count)
		{
			return { Kind::FloatVector, a_data, a_count, ImGuiDataType_COUNT };
		}
		static Value Scalar(void* a_data, ImGuiDataType a_type)
		{
			return { Kind::Scalar, a_data, 1, a_type };
		}
	};

	/// Scratch for one widget value of any intercepted kind, sized for the widest control.
	struct ValueStorage
	{
		alignas(std::uint64_t) std::byte bytes[sizeof(float) * Value::kMaxComponents]{};
	};

	/// Whether this call owns the gutter: a radio group's buttons share one address, so members defer to
	/// one toggle (see SceneWidgetInterceptor radio path).
	enum class GutterPolicy : std::uint8_t
	{
		Owner,
		GroupMember
	};

	/// How the bound control resolved this frame. Kept public so the deferred winning/losing
	/// colouring can read it without reshaping the guard.
	enum class State : std::uint8_t
	{
		Unsupported,  // the interceptor cannot bind it: behaves exactly like the normal menu
		Unbound,      // no scene can hold it: greyed for good, never bindable anywhere
		Unavailable,  // scene-controllable, but not by this scene type: greyed, never bindable here
		Absent,       // scene-controllable, no entry yet
		Overwritten,  // no user entry: a mod's overwrite supplies the value
		Active,       // entry exists and applies
		Paused,       // entry exists and is held back
		Deleted,      // a tombstone removes the mod's value here, so the layers beneath show through
		Baseline      // the main menu's control: edits the feature's base and shows which layer wins
	};

	/// Which layer a main-menu control's value comes from, highest precedence last.
	enum class BaselineLayer : std::uint8_t
	{
		Base,    // the default value
		Pack,    // a Baseline preset supplies it
		Edited,  // the user's edit over the default or a Baseline preset's value
		Scene,   // a Scene Manager preset supplies it
		Sketch   // edited over the Scene Manager's value, until the menu moves on
	};

	/// Wraps one intercepted widget call for the duration of that call.
	class Guard
	{
	public:
		Guard(const char* a_label, const Value& a_value, GutterPolicy a_policy = GutterPolicy::Owner);
		~Guard();

		Guard(const Guard&) = delete;
		Guard& operator=(const Guard&) = delete;

		/// Pointer the real ImGui call must bind: the caller's storage, or the paused holding value.
		void* Raw();
		/** @brief Raw(), typed for the matching ImGui call. */
		bool* Bool();
		int* Int();
		float* Float();

		/** @brief Closes the disabled scope, commits any edit, and draws the gutter and menu.
		 *  @return What the intercepted function should return: never true while paused. */
		bool Finish(bool a_changed);

		/** @brief How the control resolved this frame. */
		State GetState() const { return state; }

	private:
		/// Greys the control for the rest of the call, so an unbindable one cannot edit the base value.
		void OpenDisabled();

		/// One catalog component behind this control, and the entries that persist it.
		struct Component
		{
			const SceneSettingsCatalog::SettingMetadata* setting = nullptr;
			std::string settingKey;
			/// Component of the caller's storage this entry drives.
			std::uint8_t widgetComponent = 0;
			/// Owning entry per period; only the periods this control writes are filled.
			std::array<std::optional<size_t>, SceneSettingsManager::kPeriodCount> periodEntries{};
		};

		/// Collects the catalog components the control covers, each with the entries behind it.
		void ResolveComponents();

		/// The main menu's path: binds the live member and colours the control by the layer winning it.
		void BindBaseline();
		/**
		 * @brief Highest layer behind a main-menu control: sketch, scene, the user's edit, Baseline preset, else default.
		 * @param a_packId Receives the Baseline pack supplying the value beneath, or is cleared.
		 */
		BaselineLayer ResolveBaselineLayer(std::string& a_packId) const;
		/** @brief Tint for baselineLayer, nothing for Base. */
		std::optional<ImVec4> ResolveBaselineColor() const;
		/** @brief Tooltip for baselineLayer, empty for Base. */
		std::string ResolveBaselineTooltip() const;
		/// Jumps to the Scene Manager page supplying the value, or commits a sketch into it.
		void DrawBaselineGutter();

		/// A component's own address; `identity` answers for the first component only.
		SceneSettingsManager::SettingIdentity ComponentIdentity(const Component& a_component) const;
		/// Derives the state and the mixed flag from the resolved entries.
		void ResolveState();
		/// Provenance across every covered period, any user entry winning as in ResolveState.
		/// `identity` speaks for the whole aggregate through its first component.
		SceneSettingsManager::SettingLayer ResolveWinningLayer() const;

		/// Highest layer beneath this page supplying the address, once this page supplies nothing
		/// itself. None when the layers below leave the feature's base in place.
		SceneSettingsManager::SettingLayer ResolveLowerLayer() const;

		/// Highest layer resolving after this page that supplies the address, so whatever this control
		/// shows never reaches the scene here. None when this page has the last word.
		SceneSettingsManager::SettingLayer ResolveUpperLayer() const;

		/// Whether a layer stands for a value that reaches the scene, rather than a tombstone or nothing.
		static bool SuppliesValue(SceneSettingsManager::SettingLayer a_layer)
		{
			return a_layer == SceneSettingsManager::SettingLayer::Overwrite ||
			       a_layer == SceneSettingsManager::SettingLayer::User;
		}

		/// Bit per period slot this control reads. A page with no period of its own reads the period
		/// running now, since that is the only one of a periodic layer that reaches the scene.
		std::uint8_t CoveredPeriodMask() const;

		/// Colour standing for where the value comes from, or nothing when the feature's base wins.
		/// Shared by the control's tint and the gutter's toggle so the two never disagree.
		std::optional<ImVec4> ResolveProvenanceColor() const;

		/// Sentence describing what holds this value, shared by the gutter's toggle and the control.
		std::string ResolveStatusTooltip() const;
		/// Opens the page that can actually edit a greyed Unbound/Unavailable setting.
		void NavigateGreyedSetting() const;

		/** @brief Applies the same enable path as ticking the left override checkbox. */
		void EnableOverride();
		/** @brief Double-click / Ctrl+click / Shift+click on a greyed control ticks the override on.
		 *  @return True when the gesture fired and the override was enabled. */
		bool TryEnableOverrideFromGesture();
		/** @brief Middle-click on a main-menu control writes back the feature's code default.
		 *  @return True when the gesture fired and changed the value. */
		bool TryResetFromGesture();

		/** @brief Writes an edit into this control's entries, reviving a tombstone and creating missing entries first. */
		void Commit();
		/** @brief Draws the leading marker column.
		 *  @return Whether it changed which entries exist or whether they apply, so the caller can
		 *          re-resolve only then instead of on every control every frame. */
		bool DrawGutter();
		/// Whether this control can carry a location transition duration, by the same rule the
		/// document loader applies: a Location page, and every component transitionable.
		bool SupportsTransition() const;
		/// The gutter's third slot: the per-entry duration, or a reserved gap that keeps the column straight.
		void DrawTransitionSlot();
		/// Takes the feature's SetNextItemWidth aside, since the gutter's own items would consume it
		/// before the control it was meant for ever ran.
		void CaptureNextItemWidth();
		/// Shrinks the control by the space the leading gutter took, so it still fits the panel.
		void PushCompensatedItemWidth();
		/** @brief Undoes PushCompensatedItemWidth, if it pushed. */
		void PopCompensatedItemWidth();
		/** @brief Right-click menu: the stored value, revert, delete or restore the mod's value. */
		void DrawContextMenu();

		/// Drops every entry this control owns, shared by the gutter's remove button and the
		/// context menu's "Delete override" item.
		void DeleteOverride();

		/** @brief Tombstones or clears every covered period of every component this control spans.
		 *  @param a_tombstoned Whether the address ends up suppressed. */
		void SetTombstoned(bool a_tombstoned);

		/// Whether a period slot is one this control reads and writes.
		bool IsCoveredSlot(int a_slot) const;
		/** @brief Whether every component has an entry in every covered slot. */
		bool HasAllCoveredEntries() const;

		/** @brief Runs a_visit over every component and covered period slot, with the context that
		 *  slot's entry lives in. */
		template <typename Visitor>
		void ForEachCoveredSlot(Visitor&& a_visit) const
		{
			for (const auto& component : components)
				for (int slot = 0; slot < SceneSettingsManager::kPeriodCount; ++slot) {
					if (!IsCoveredSlot(slot))
						continue;
					a_visit(component, slot, contextId);
				}
		}

		/** @brief Creates the entries the covered periods are still missing.
		 *  @return Whether the control owns at least one entry afterwards. */
		bool EnsureEntries(bool a_deferSave);

		/// The entry a component displays: the armed period's, or the first period holding one.
		std::optional<size_t> PrimaryEntry(const Component& a_component) const;

		/// The entry supplying what this page would apply at a component's address: its own user entry,
		/// or the overwrite standing in when it has none.
		std::optional<size_t> DisplayEntry(const Component& a_component) const;

		/// Every entry this control owns, across components and periods.
		std::vector<size_t> CollectOwnedEntries() const;

		/// Drops the resolved entries once they are deleted, so the rest of the frame reads Absent.
		void ForgetEntries();

		/// Points the control at this page's own value, never the live member the scene resolves into.
		void BindDisplayValue();

		/// Storage the control was bound to, and so the one an edit landed in.
		const void* BoundData() const;

		/// Loads what this page would apply into the holding storage the bound control reads.
		void StoreHoldingValue();
		/** @brief What a component would fall back on were this page active and holding nothing: the
		 *  highest lower layer supplying it, else the feature's base.
		 *  @return Null when the live member already holds that base. */
		const json* ResolveFallbackValue(const Component& a_component) const;
		/** @brief Writes a stored value into the component's slot of a_storage, in widget scale. */
		void WriteComponent(void* a_storage, const Component& a_component, const json& a_stored) const;

		/// The caller's post-call storage, as the primitive one component persists.
		json ReadEditedValue(const Component& a_component) const;

		/// The stored override on one line; an aggregate lists every component.
		std::string DescribeStoredValue() const;

		/** @brief The edited value of each component, for every entry that component owns. */
		std::vector<SceneSettingsManager::EntryValueUpdate> BuildEntryValueUpdates() const;

		const char* label;
		Value value;
		GutterPolicy policy;
		State state = State::Unsupported;
		BaselineLayer baselineLayer = BaselineLayer::Base;
		/// The Baseline pack supplying the value beneath the control, for its tooltip.
		std::string baselinePackId;
		/// Layer winning at this address, which drives the colour independently of `state`. A paused
		/// user entry stays Paused so the checkbox has something to resume, but reads as the mod's.
		SceneSettingsManager::SettingLayer winningLayer = SceneSettingsManager::SettingLayer::None;
		/// Layer beneath this page supplying the address, resolved only when this page supplies
		/// nothing. This page outranks it, so it only names the value's origin in the tooltip.
		SceneSettingsManager::SettingLayer lowerLayer = SceneSettingsManager::SettingLayer::None;
		/// Layer resolving after this page that outranks what the control shows. Resolved only once
		/// the control shows a value at all, so a non-None value means "this loses here".
		SceneSettingsManager::SettingLayer upperLayer = SceneSettingsManager::SettingLayer::None;
		std::size_t valueSize = 0;
		/// Widget value = persisted value * widgetScale; only a proxied control scales.
		float widgetScale = 1.0f;

		const SceneSettingsCatalog::SettingMetadata* metadata = nullptr;
		SceneSettingsManager::SceneContextId contextId;
		std::optional<size_t> entryIndex;

		/// Feature, path and the first component's key. Resolved once so neither the commit path nor
		/// the layer queries rebuild the catalog address per control per frame.
		SceneSettingsManager::SettingIdentity identity;
		std::vector<Component> components;

		/// Period slot the armed context edits; 0 for a flat set, which has no periods.
		int armedSlot = 0;
		/// The periods and components this control spans do not agree on a value or on coverage.
		bool mixedAcrossPeriods = false;

		/// The member before the call, so entry creation can undo an unbound control's write before the
		/// manager snapshots the feature's base.
		ValueStorage preCall;
		/// Storage a bound scene control reads and writes, so no write reaches the feature member.
		ValueStorage holding;
		/// Whether the control was bound to `holding` rather than the caller's storage.
		bool boundToHolding = false;

		bool commitDeferred = false;
		bool disabledOpened = false;
		bool mixedFlagPushed = false;
		bool tintPushed = false;

		/// Horizontal space the leading gutter took, measured so the control can be shrunk by it.
		float gutterConsumedWidth = 0.0f;
		bool itemWidthPushed = false;
		/// The feature's own SetNextItemWidth, held across the gutter and reapplied trimmed.
		float capturedNextItemWidth = 0.0f;
		bool nextItemWidthCaptured = false;
	};
}
