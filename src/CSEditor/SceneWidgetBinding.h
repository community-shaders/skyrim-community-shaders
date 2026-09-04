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

	/// Writes a number into a scalar of the given ImGui data type; no-op for a type the intercepted
	/// scalar widgets don't accept. Shared so palette value drops write the same way the widgets do.
	void WriteScalarValue(void* a_destination, ImGuiDataType a_type, double a_value);

	/// Scratch for one widget value of any intercepted kind, sized for the widest control.
	struct ValueStorage
	{
		alignas(std::uint64_t) std::byte bytes[sizeof(float) * Value::kMaxComponents]{};
	};

	/// Whether this call owns the gutter. A radio group is several calls against one address, so
	/// its members defer ownership rather than drawing one toggle per button. GroupMember has no
	/// intercepted feature drawing a radio group yet; dropping it would leave a double-gutter bug
	/// waiting for the first one.
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
		Deleted       // a tombstone suppresses every lower layer at this address
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
		bool* Bool();
		int* Int();
		float* Float();

		/** @brief Closes the disabled scope, commits any edit, and draws the gutter and menu.
		 *  @return What the intercepted function should return: never true while paused. */
		bool Finish(bool a_changed);

		State GetState() const { return state; }

		const SceneSettingsCatalog::SettingMetadata* GetMetadata() const { return metadata; }

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
		/// Derives the state and the mixed flag from the resolved entries.
		void ResolveState();
		/// Provenance across every period this control covers, combined "any user wins" like
		/// ResolveState's anyActive/anyPaused. An aggregate shares one address family, so `identity`
		/// answering for its first component answers for all of them.
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
		const char* ResolveStatusTooltip() const;

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
		void PopCompensatedItemWidth();
		void DrawContextMenu();

		/// Drops every entry this control owns, shared by the gutter's remove button and the
		/// context menu's "Delete override" item.
		void DeleteOverride();

		/** @brief Tombstones or clears every covered period of every component this control spans.
		 *  @param a_tombstoned Whether the address ends up suppressed. */
		void SetTombstoned(bool a_tombstoned);

		/// Whether a period slot is one this control reads and writes.
		bool IsCoveredSlot(int a_slot) const;
		bool HasAllCoveredEntries() const;

		/** @brief Runs a_visit over every component and covered period slot, with the context that
		 *  slot's entry lives in. The manager is period-scoped, so a control writing every period at
		 *  once has to name each one itself. */
		template <typename Visitor>
		void ForEachCoveredSlot(Visitor&& a_visit) const
		{
			for (const auto& component : components)
				for (int slot = 0; slot < SceneSettingsManager::kPeriodCount; ++slot) {
					if (!IsCoveredSlot(slot))
						continue;
					auto slotContext = contextId;
					if (flatAcrossPeriods)
						slotContext.period = SceneSettingsManager::kPeriods[static_cast<size_t>(slot)];
					a_visit(component, slot, slotContext);
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

		/// Points the control at this page's own value whenever the live member carries another layer's.
		void BindDisplayValue();

		/// Storage the control was bound to, and so the one an edit landed in.
		const void* BoundData() const;

		/// Loads what this page would apply into the holding storage the bound control reads.
		void StoreHoldingValue();
		void WriteHoldingComponent(const Component& a_component, const json& a_stored);

		/// The caller's post-call storage, as the primitive one component persists.
		json ReadEditedValue(const Component& a_component) const;

		/// The stored override on one line; an aggregate lists every component.
		std::string DescribeStoredValue() const;

		std::vector<SceneSettingsManager::EntryValueUpdate> BuildEntryValueUpdates() const;

		const char* label;
		Value value;
		GutterPolicy policy;
		State state = State::Unsupported;
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

		/// Period slot the armed context edits; 0 for a context that has no periods.
		int armedSlot = 0;
		/// One edit writes every period, which is what "time of day off" means.
		bool flatAcrossPeriods = false;
		/// The periods and components this control spans do not agree on a value or on coverage.
		bool mixedAcrossPeriods = false;

		/// What the control showed before the call, so entry creation can restore it before the manager
		/// snapshots the member. Re-seeded from `holding` once a layer above shadows this page.
		ValueStorage preCall;
		/// Storage a displaced control is bound to, so no write reaches the feature member.
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
