#include "SceneWidgetBinding.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <utility>

#include <imgui_internal.h>

#include "../I18n/I18n.h"
#include "Menu.h"
#include "SceneTransitionField.h"
#include "SceneWidgetInterceptor.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using Kind = SceneWidgetBinding::Value::Kind;
	using SettingMetadata = SceneSettingsCatalog::SettingMetadata;

	/// Smaller than the location menu's delete icon (SceneSettingsUI.cpp): the gutter sits inline
	/// with a checkbox rather than a table row, so the icon needs to read as the lighter action.
	constexpr float kRemoveIconScale = 0.75f;

	/// A control shrunk to make room for the gutter never goes below this, so a narrow panel keeps
	/// a slider you can still aim at.
	constexpr float kMinCompensatedItemWidth = 60.0f;

	constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

	/// Reads and writes one ImGui scalar through a double. A zero size means the type is not one
	/// the scalar widgets accept, which the binding treats as an unresolvable control.
	struct ScalarTraits
	{
		std::size_t size = 0;
		double (*read)(const void*) = nullptr;
		void (*write)(void*, double) = nullptr;
	};

	template <typename T>
	constexpr ScalarTraits MakeScalarTraits()
	{
		return { sizeof(T),
			[](const void* source) { return static_cast<double>(*static_cast<const T*>(source)); },
			[](void* destination, double number) { *static_cast<T*>(destination) = static_cast<T>(number); } };
	}

	ScalarTraits GetScalarTraits(ImGuiDataType type)
	{
		switch (type) {
		case ImGuiDataType_S8:
			return MakeScalarTraits<std::int8_t>();
		case ImGuiDataType_U8:
			return MakeScalarTraits<std::uint8_t>();
		case ImGuiDataType_S16:
			return MakeScalarTraits<std::int16_t>();
		case ImGuiDataType_U16:
			return MakeScalarTraits<std::uint16_t>();
		case ImGuiDataType_S32:
			return MakeScalarTraits<std::int32_t>();
		case ImGuiDataType_U32:
			return MakeScalarTraits<std::uint32_t>();
		case ImGuiDataType_S64:
			return MakeScalarTraits<std::int64_t>();
		case ImGuiDataType_U64:
			return MakeScalarTraits<std::uint64_t>();
		case ImGuiDataType_Float:
			return MakeScalarTraits<float>();
		case ImGuiDataType_Double:
			return MakeScalarTraits<double>();
		default:
			return {};
		}
	}

	/// Bytes the caller's storage occupies, or zero when the control cannot be bound.
	std::size_t ValueSize(const SceneWidgetBinding::Value& value)
	{
		switch (value.kind) {
		case Kind::Bool:
			return sizeof(bool);
		case Kind::Int:
			return sizeof(int);
		case Kind::Float:
		case Kind::FloatVector:
			return value.componentCount <= SceneWidgetBinding::Value::kMaxComponents ?
			           sizeof(float) * value.componentCount :
			           0;
		case Kind::Scalar:
			return GetScalarTraits(value.scalarType).size;
		default:
			return 0;
		}
	}

	using ComponentGroup = std::vector<const SettingMetadata*>;

	// Same grouping the manager uses to fold stored scalars back into one aggregate control, plus
	// settingPath so two controls over one serialized member stay apart.
	using AggregateKey = std::tuple<std::string_view, std::string_view, std::string_view,
		std::string_view, SceneSettingsCatalog::AggregateSemantic, std::int8_t, std::uint8_t>;

	AggregateKey MakeAggregateKey(const SettingMetadata& setting)
	{
		return { setting.featureShortName, setting.settingPath, setting.serializedPath,
			setting.serializedKey, setting.aggregateSemantic, setting.aggregateStart,
			setting.aggregateCount };
	}

	/// The catalog rows one control drives: its own row alone, or every sibling of its aggregate.
	ComponentGroup GetControlComponents(const SettingMetadata& setting)
	{
		// A single-component row is its own control even when it sits inside a vector member, so
		// the four sliders over one float4 must not be folded into one aggregate.
		if (setting.aggregateCount <= 1 || setting.aggregateAll)
			return { &setting };

		static const std::map<AggregateKey, ComponentGroup> groups = [] {
			std::map<AggregateKey, ComponentGroup> built;
			for (const auto& candidate : SceneSettingsCatalog::GetSettings()) {
				if (candidate.aggregateCount <= 1 || candidate.aggregateAll)
					continue;
				built[MakeAggregateKey(candidate)].push_back(&candidate);
			}
			for (auto& [key, group] : built)
				std::ranges::sort(group, {}, &SettingMetadata::serializedComponent);
			return built;
		}();

		const auto found = groups.find(MakeAggregateKey(setting));
		return found != groups.end() ? found->second : ComponentGroup{ &setting };
	}

	int gutterFrame = -1;
	/// Keyed by window as well as address: two panels drawing the same feature must each get their
	/// own gutter instead of the second panel finding the first's claim already taken.
	using GutterKey = std::pair<ImGuiID, const void*>;
	std::map<GutterKey, int> gutterCalls;

	/// A leading gutter's owner is the first call against a (window, address) pair in a frame, so a
	/// radio group draws a single toggle ahead of its first button.
	bool ClaimGutter(const void* address)
	{
		if (const auto frame = ImGui::GetFrameCount(); frame != gutterFrame) {
			gutterFrame = frame;
			gutterCalls.clear();
		}
		const GutterKey key{ ImGui::GetCurrentWindow()->ID, address };
		return ++gutterCalls[key] == 1;
	}

	using SceneContextId = SceneSettingsManager::SceneContextId;
	using SceneContextType = SceneSettingsManager::SceneContextType;
	using SettingLayer = SceneSettingsManager::SettingLayer;

	/// Which layer supplies one address, per period. An aperiodic context fills every slot, so a
	/// periodic page above it reads its own period out of the same row.
	using PeriodLayers = std::array<SettingLayer, kPeriodCount>;
	using LayerIndex = std::map<SceneSettingsManager::SettingIdentity, PeriodLayers>;

	/// One collected stack per page per frame. Which layers sit under or over a page depends on the
	/// live scene rather than on the control, so every widget on the page shares one build.
	template <auto Collect>
	const std::vector<SceneContextId>& GetFrameContextStack(const SceneContextId& a_page)
	{
		static std::map<SceneContextId, std::vector<SceneContextId>> cache;
		static int cachedFrame = -1;

		if (const auto frame = ImGui::GetFrameCount(); frame != cachedFrame) {
			cachedFrame = frame;
			cache.clear();
		}
		const auto found = cache.find(a_page);
		return found != cache.end() ? found->second : cache.emplace(a_page, Collect(a_page)).first->second;
	}

	/// The layers a page sits on top of, highest first: weather resolves over time of day, and a
	/// location over whichever stack is running. Interior and the exterior stack never resolve at the
	/// same time, so the live cell picks the one a location sits on, exactly as the resolver does.
	/// Periods are filled in because a context is only fetchable with a valid one.
	std::vector<SceneContextId> CollectLowerContexts(const SceneContextId& a_page)
	{
		std::vector<SceneContextId> lower;
		if (a_page.type == SceneContextType::Location && Util::IsInterior()) {
			lower.push_back({ .type = SceneContextType::Interior });
			return lower;
		}

		switch (a_page.type) {
		case SceneContextType::Location:
			if (const auto* sky = globals::game::sky; sky && sky->currentWeather)
				lower.push_back({ .type = SceneContextType::Weather,
					.period = SceneSettingsManager::kPeriods[0],
					.weatherId = sky->currentWeather->GetFormID() });
			[[fallthrough]];
		case SceneContextType::Weather:
			lower.push_back({ .type = SceneContextType::TimeOfDay, .period = SceneSettingsManager::kPeriods[0] });
			break;
		default:
			break;
		}
		return lower;
	}

	const std::vector<SceneContextId>& GetLowerContexts(const SceneContextId& a_page)
	{
		return GetFrameContextStack<CollectLowerContexts>(a_page);
	}

	/// Fills one feature's rows from a context, highest-ranking source last so a user entry shadows
	/// the overwrite it was authored over.
	void CollectContextLayers(const SceneContextId& a_context, const std::string& a_feature, LayerIndex& a_index)
	{
		using EntrySource = SceneSettingsManager::EntrySource;
		auto* manager = SceneSettingsManager::GetSingleton();

		for (const auto source : { EntrySource::Overwrite, EntrySource::User }) {
			for (const auto& entry : manager->GetContextEntries(a_context)) {
				if (entry.source != source || entry.paused || entry.featureShortName != a_feature)
					continue;
				auto layer = source == EntrySource::Overwrite ? SettingLayer::Overwrite : SettingLayer::User;
				if (entry.deleted)
					layer = SettingLayer::Deleted;

				auto& row = a_index[{ entry.featureShortName, entry.settingPath, entry.settingKey }];
				if (const auto period = static_cast<size_t>(entry.period); period < static_cast<size_t>(kPeriodCount))
					row[period] = layer;
				else
					row.fill(layer);
			}
		}
	}

	/// Layers supplying one feature's addresses in one context. Every control on a page asks this of
	/// the same entries, and the walk is the whole context, so it is cached until the entries change.
	const LayerIndex& GetContextLayerIndex(const SceneContextId& a_context, const std::string& a_feature)
	{
		static std::map<std::pair<SceneContextId, std::string>,
			SceneSettingsManager::RevisionCache<LayerIndex>>
			cache;

		auto* manager = SceneSettingsManager::GetSingleton();
		return cache[{ a_context, a_feature }].Get(manager->GetEntryPresentationRevision(), [&] {
			LayerIndex layers;
			// Nothing a paused feature holds applies, so its every context reads as empty.
			if (!manager->IsFeaturePaused(a_feature))
				CollectContextLayers(a_context, a_feature, layers);
			return layers;
		});
	}

	/// Where one feature's user entries sit in a context, per period. Same walk and same lifetime as
	/// the layer index: an entry's position only moves when the entries themselves do.
	using PeriodEntries = std::array<std::optional<size_t>, kPeriodCount>;
	using UserEntryIndex = std::map<SceneSettingsManager::SettingIdentity, PeriodEntries>;

	const UserEntryIndex& GetContextUserEntryIndex(const SceneContextId& a_context, const std::string& a_feature)
	{
		static std::map<std::pair<SceneContextId, std::string>,
			SceneSettingsManager::RevisionCache<UserEntryIndex>>
			cache;

		auto* manager = SceneSettingsManager::GetSingleton();
		return cache[{ a_context, a_feature }].Get(manager->GetEntryPresentationRevision(), [&] {
			UserEntryIndex index;
			// An aperiodic context stores one entry with no period of its own, so it fills slot 0.
			const bool periodic = SceneSettingsManager::IsPeriodicContext(a_context.type);
			const auto entries = manager->GetContextEntries(a_context);
			for (size_t position = 0; position < entries.size(); ++position) {
				const auto& entry = entries[position];
				if (entry.source != SceneSettingsManager::EntrySource::User ||
					entry.featureShortName != a_feature)
					continue;
				if (const auto slot = periodic ? static_cast<size_t>(entry.period) : 0;
					slot < static_cast<size_t>(kPeriodCount))
					index[{ entry.featureShortName, entry.settingPath, entry.settingKey }][slot] = position;
			}
			return index;
		});
	}

	/// The location targets resolving after a page: the whole running chain, or for a location page
	/// only the narrower links that follow its own target. A page off the live chain has none above it.
	void AppendUpperLocationContexts(const SceneContextId& a_page, std::vector<SceneContextId>& a_upper)
	{
		const auto& targets = SceneSettingsManager::GetSingleton()->GetCurrentLocationTargets();
		auto target = targets.begin();
		if (a_page.type == SceneContextType::Location) {
			target = std::ranges::find_if(targets, [&a_page](const auto& candidate) {
				return candidate.type == a_page.locationType && candidate.formKey == a_page.locationFormKey;
			});
			if (target == targets.end())
				return;
			++target;
		}
		for (; target != targets.end(); ++target)
			a_upper.push_back({ .type = SceneContextType::Location,
				.locationType = target->type,
				.locationFormKey = target->formKey });
	}

	/// The layers a page is resolved under, lowest first: weather resolves over time of day, and the
	/// location chain over whichever stack is running. Mirror image of CollectLowerContexts, so a page
	/// belonging to the stack the live scene is not running has nothing above it.
	std::vector<SceneContextId> CollectUpperContexts(const SceneContextId& a_page)
	{
		std::vector<SceneContextId> upper;
		const bool interior = Util::IsInterior();

		switch (a_page.type) {
		case SceneContextType::Interior:
			if (!interior)
				return upper;
			break;
		case SceneContextType::TimeOfDay:
			if (interior)
				return upper;
			if (const auto* sky = globals::game::sky; sky && sky->currentWeather)
				upper.push_back({ .type = SceneContextType::Weather,
					.period = SceneSettingsManager::kPeriods[0],
					.weatherId = sky->currentWeather->GetFormID() });
			break;
		case SceneContextType::Weather:
			if (interior)
				return upper;
			break;
		case SceneContextType::Location:
			break;
		default:
			return upper;
		}

		AppendUpperLocationContexts(a_page, upper);
		return upper;
	}

	const std::vector<SceneContextId>& GetUpperContexts(const SceneContextId& a_page)
	{
		return GetFrameContextStack<CollectUpperContexts>(a_page);
	}

	/// One bit per period slot, so the layer walk can be told which periods to read without being
	/// handed the control that reads them.
	using PeriodMask = std::uint8_t;
	static_assert(kPeriodCount <= 8, "PeriodMask holds one bit per period");
	constexpr PeriodMask kAllPeriods = static_cast<PeriodMask>((1u << kPeriodCount) - 1u);

	/// The one period running right now, or every period while the game has no answer yet.
	PeriodMask LivePeriodMask()
	{
		const auto period = static_cast<size_t>(SceneSettingsManager::GetCurrentPeriod());
		return period < static_cast<size_t>(kPeriodCount) ? static_cast<PeriodMask>(1u << period) : kAllPeriods;
	}

	/// Highest-ranking layer one context supplies at an address, folded across the periods asked for:
	/// a user entry outranks a tombstone, which outranks an overwrite.
	SettingLayer FoldContextLayer(const SceneContextId& a_context, const std::string& a_feature,
		const SceneSettingsManager::SettingIdentity& a_identity, PeriodMask a_periods)
	{
		const auto& index = GetContextLayerIndex(a_context, a_feature);
		const auto row = index.find(a_identity);
		if (row == index.end())
			return SettingLayer::None;

		bool sawDeleted = false, sawOverwrite = false;
		for (int slot = 0; slot < kPeriodCount; ++slot) {
			if (!(a_periods & static_cast<PeriodMask>(1u << slot)))
				continue;
			switch (row->second[static_cast<size_t>(slot)]) {
			case SettingLayer::User:
				return SettingLayer::User;
			case SettingLayer::Deleted:
				sawDeleted = true;
				break;
			case SettingLayer::Overwrite:
				sawOverwrite = true;
				break;
			default:
				break;
			}
		}
		if (sawDeleted)
			return SettingLayer::Deleted;
		return sawOverwrite ? SettingLayer::Overwrite : SettingLayer::None;
	}

	/// Same scene-type split the manager applies when persisting a new entry (AddContextSetting),
	/// so a control never resolves as allowed here and then fails to gain an entry on the gutter tick.
	SceneSettingsManager::SceneType SceneTypeForContext(SceneSettingsManager::SceneContextType a_type)
	{
		using SceneContextType = SceneSettingsManager::SceneContextType;
		using SceneType = SceneSettingsManager::SceneType;
		switch (a_type) {
		case SceneContextType::Interior:
			return SceneType::InteriorOnly;
		case SceneContextType::Location:
			return SceneType::Location;
		case SceneContextType::TimeOfDay:
		case SceneContextType::Weather:
		default:
			return SceneType::TimeOfDay;
		}
	}
}

void SceneWidgetBinding::WriteScalarValue(void* a_destination, ImGuiDataType a_type, double a_value)
{
	if (const auto traits = GetScalarTraits(a_type); traits.write)
		traits.write(a_destination, a_value);
}

SceneWidgetBinding::Guard::Guard(const char* a_label, const Value& a_value, GutterPolicy a_policy) :
	label(a_label), value(a_value), policy(a_policy)
{
	assert(value.data && "intercepted widget bound a null address");
	valueSize = ValueSize(value);
	assert(valueSize <= sizeof(ValueStorage::bytes));
	if (!value.data || valueSize == 0 || valueSize > sizeof(ValueStorage::bytes))
		return;

	// Captured before any early return, so entry creation always has the base value to restore.
	std::memcpy(preCall.bytes, value.data, valueSize);

	const auto* context = SceneWidgetInterceptor::GetArmedContext();
	if (!context)
		return;

	// A Util:: wrapper may hand ImGui a rescaled temporary, so the member it stands for is what
	// resolves and what gets persisted.
	const auto* proxy = SceneWidgetInterceptor::GetArmedProxy();
	if (proxy)
		widgetScale = proxy->displayScale;

	metadata = SceneSettingsCatalog::FindSettingForControl(
		context->feature, proxy ? proxy->member : value.data);
	if (!metadata) {
		// Not a catalogued setting at all (e.g. a plain UI toggle like "Show Advanced"), so the
		// interceptor has nothing to bind. Left live rather than greyed: it never promised an
		// override in the first place.
		state = State::Unsupported;
		return;
	}
	if (!SceneSettingsManager::IsSceneSettingAllowed(
			metadata->featureShortName, metadata->settingPath, metadata->settingKey)) {
		// Barred by policy, so no scene will ever hold it. Greyed rather than left live, because an
		// edit here would rewrite the feature's base value from a panel that only promises overrides.
		state = State::Unbound;
		metadata = nullptr;
		OpenDisabled();
		return;
	}

	contextId = context->contextId;
	identity.featureShortName = std::string{ metadata->featureShortName };
	identity.settingPath = SceneSettingsManager::SplitSettingPath(metadata->settingPath);

	// Interior and Location store one entry with no period, so only a periodic context can fan out.
	const bool periodic = SceneSettingsManager::IsPeriodicContext(contextId.type);
	flatAcrossPeriods = periodic && !context->perPeriod;
	if (const auto period = static_cast<int>(contextId.period);
		periodic && period >= 0 && period < kPeriodCount)
		armedSlot = period;

	ResolveComponents();
	if (components.empty()) {
		// A scene setting this context cannot hold, e.g. a non-transitionable one under time of day.
		state = State::Unavailable;
		OpenDisabled();
		return;
	}

	ResolveState();
	BindDisplayValue();

	// Leading gutter: the marker column reads before the control, and a click lands in the same
	// frame rather than a frame late.
	if (policy == GutterPolicy::Owner || ClaimGutter(value.data)) {
		CaptureNextItemWidth();
		// Only a gutter that changed the entries makes the resolved state stale.
		if (DrawGutter()) {
			ResolveState();
			BindDisplayValue();
		}
	}
	PushCompensatedItemWidth();

	if (state == State::Paused)
		OpenDisabled();
	if (mixedAcrossPeriods && value.kind == Kind::Bool) {
		// Only Checkbox honours the flag; every other kind gets the tinted gutter instead.
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		mixedFlagPushed = true;
	}

	// The control itself carries the provenance too, so a slider reads without tracing back to its gutter.
	if (const auto tint = ResolveProvenanceColor()) {
		Util::PushTintedFrameStyle(*tint);
		tintPushed = true;
	}
}

SceneWidgetBinding::Guard::~Guard()
{
	// Finish always runs on the happy path; this only closes scopes an exception skipped.
	if (tintPushed)
		Util::PopTintedFrameStyle();
	if (mixedFlagPushed)
		ImGui::PopItemFlag();
	if (disabledOpened)
		ImGui::EndDisabled();
	if (itemWidthPushed)
		ImGui::PopItemWidth();
}

void* SceneWidgetBinding::Guard::Raw()
{
	return const_cast<void*>(BoundData());
}

const void* SceneWidgetBinding::Guard::BoundData() const
{
	return boundToHolding ? static_cast<const void*>(holding.bytes) : value.data;
}

bool* SceneWidgetBinding::Guard::Bool()
{
	return static_cast<bool*>(Raw());
}

int* SceneWidgetBinding::Guard::Int()
{
	return static_cast<int*>(Raw());
}

float* SceneWidgetBinding::Guard::Float()
{
	return static_cast<float*>(Raw());
}

void SceneWidgetBinding::Guard::ResolveComponents()
{
	// The resolver answers a control's base address with its first component, so an aggregate has
	// to walk out to its siblings: each one is a separate entry keyed by its own settingKey.
	const auto start = std::max<int>(metadata->aggregateStart, 0);
	const auto sceneType = SceneTypeForContext(contextId.type);

	for (const auto* setting : GetControlComponents(*metadata)) {
		// Siblings share featureShortName/settingPath (see MakeAggregateKey), so the guard's own
		// resolved values apply to every component here; only settingKey varies.
		if (!SceneSettingsManager::IsSettingAllowedForType(
				sceneType, identity.featureShortName, identity.settingPath, std::string{ setting->settingKey }))
			continue;

		const auto slot = setting->aggregateCount <= 1 ?
		                      0 :
		                      static_cast<int>(setting->serializedComponent) - start;
		if (slot < 0 || slot >= value.componentCount)
			continue;

		components.push_back(
			Component{ setting, std::string{ setting->settingKey }, static_cast<std::uint8_t>(slot), {} });
	}

	// The layer queries speak for the whole aggregate through its first component's address.
	if (!components.empty())
		identity.settingKey = components.front().settingKey;
}

void SceneWidgetBinding::Guard::ResolveState()
{
	auto* manager = SceneSettingsManager::GetSingleton();
	const auto entries = manager->GetContextEntries(contextId);

	entryIndex.reset();
	mixedAcrossPeriods = false;
	lowerLayer = SceneSettingsManager::SettingLayer::None;
	upperLayer = SceneSettingsManager::SettingLayer::None;

	bool anyActive = false;
	bool anyPaused = false;
	bool anyMissing = false;
	bool anyDeleted = false;
	bool anyLiveValue = false;  // a covered slot holding a real value: unpaused and not a tombstone

	// Every component shares the guard's feature and path, so one key swap per component walks the
	// index the whole page already built.
	const auto& userEntries = GetContextUserEntryIndex(contextId, identity.featureShortName);
	auto lookup = identity;

	for (auto& component : components) {
		lookup.settingKey = component.settingKey;
		const auto found = userEntries.find(lookup);
		component.periodEntries = found != userEntries.end() ? found->second : PeriodEntries{};

		const json* reference = nullptr;
		for (int slot = 0; slot < kPeriodCount; ++slot) {
			auto& resolved = component.periodEntries[static_cast<size_t>(slot)];
			if (!IsCoveredSlot(slot)) {
				resolved.reset();
				continue;
			}
			if (!resolved || *resolved >= entries.size()) {
				resolved.reset();
				anyMissing = true;
				continue;
			}

			const auto& entry = entries[*resolved];
			// A paused tombstone suppresses nothing, so the state must not claim the value is gone.
			if (entry.deleted && !entry.paused)
				anyDeleted = true;
			if (entry.paused)
				anyPaused = true;
			else
				anyActive = true;
			if (!entry.paused && !entry.deleted)
				anyLiveValue = true;
			// A tombstone's value is stale and meaningless; never compare against it.
			if (!entry.deleted) {
				if (!reference)
					reference = &entry.value;
				else if (*reference != entry.value)
					mixedAcrossPeriods = true;
			}
		}
	}

	// Nothing left to resolve, and the provenance query below needs a component to key off of.
	if (components.empty())
		return;

	// Provenance is a separate axis from state: it says who wins, state says what the user's own
	// entry is doing.
	winningLayer = ResolveWinningLayer();

	if (!anyActive && !anyPaused) {
		state = winningLayer == SceneSettingsManager::SettingLayer::Overwrite ? State::Overwritten : State::Absent;
		// Nothing in this context holds the address, so a layer under this page may be driving it and
		// the page has to say so. A tombstone here already suppressed everything below.
		if (winningLayer == SceneSettingsManager::SettingLayer::None)
			lowerLayer = ResolveLowerLayer();
	} else {
		// A partly covered or partly paused control is as mixed as one whose periods hold two values.
		state = anyDeleted ? State::Deleted : (anyActive ? State::Active : State::Paused);
		mixedAcrossPeriods = mixedAcrossPeriods || anyMissing || (anyActive && anyPaused) || (anyDeleted && anyLiveValue);

		for (const auto& component : components) {
			entryIndex = PrimaryEntry(component);
			if (entryIndex)
				break;
		}
	}

	// A control showing a value can lose it, and an empty one still has to say when the number in its
	// box belongs to a layer above. A tombstone or a paused entry applies either way, so neither asks.
	if (SuppliesValue(winningLayer) || state == State::Absent)
		upperLayer = ResolveUpperLayer();
}

SceneSettingsManager::SettingLayer SceneWidgetBinding::Guard::ResolveWinningLayer() const
{
	return FoldContextLayer(contextId, identity.featureShortName, identity, CoveredPeriodMask());
}

std::uint8_t SceneWidgetBinding::Guard::CoveredPeriodMask() const
{
	// An aperiodic page spans the whole day, but only the running period of a periodic layer reaches
	// the scene, so folding all six would let an off-hours entry answer for right now.
	if (!SceneSettingsManager::IsPeriodicContext(contextId.type))
		return LivePeriodMask();

	PeriodMask mask = 0;
	for (int slot = 0; slot < kPeriodCount; ++slot)
		if (IsCoveredSlot(slot))
			mask = static_cast<PeriodMask>(mask | (1u << slot));
	return mask;
}

SceneSettingsManager::SettingLayer SceneWidgetBinding::Guard::ResolveLowerLayer() const
{
	const auto periods = CoveredPeriodMask();

	for (const auto& lower : GetLowerContexts(contextId)) {
		switch (FoldContextLayer(lower, identity.featureShortName, identity, periods)) {
		case SettingLayer::User:
			return SettingLayer::User;
		// A tombstone suppresses the layers under it and then supplies nothing itself, so this page
		// is left free to author here.
		case SettingLayer::Deleted:
			return SettingLayer::None;
		case SettingLayer::Overwrite:
			return SettingLayer::Overwrite;
		default:
			break;
		}
	}
	return SettingLayer::None;
}

SceneSettingsManager::SettingLayer SceneWidgetBinding::Guard::ResolveUpperLayer() const
{
	const auto periods = CoveredPeriodMask();

	// Highest first: the topmost supplier is the one that reaches the scene, and a tombstone up there
	// suppresses everything beneath it, this page included.
	const auto& upper = GetUpperContexts(contextId);
	for (auto context = upper.rbegin(); context != upper.rend(); ++context)
		if (const auto layer = FoldContextLayer(*context, identity.featureShortName, identity, periods);
			layer != SettingLayer::None)
			return layer;
	return SettingLayer::None;
}

bool SceneWidgetBinding::Guard::IsCoveredSlot(int a_slot) const
{
	return flatAcrossPeriods || a_slot == armedSlot;
}

bool SceneWidgetBinding::Guard::HasAllCoveredEntries() const
{
	for (const auto& component : components)
		for (int slot = 0; slot < kPeriodCount; ++slot)
			if (IsCoveredSlot(slot) && !component.periodEntries[static_cast<size_t>(slot)])
				return false;
	return true;
}

std::optional<size_t> SceneWidgetBinding::Guard::PrimaryEntry(const Component& a_component) const
{
	if (const auto armed = a_component.periodEntries[static_cast<size_t>(armedSlot)])
		return armed;
	for (int slot = 0; slot < kPeriodCount; ++slot)
		if (const auto index = a_component.periodEntries[static_cast<size_t>(slot)])
			return index;
	return std::nullopt;
}

std::optional<size_t> SceneWidgetBinding::Guard::DisplayEntry(const Component& a_component) const
{
	if (const auto owned = PrimaryEntry(a_component))
		return owned;

	// No user entry, so an overwrite in this context is what the page would apply. The last one
	// discovered wins, exactly as GetSettingProvenance folds them.
	std::optional<size_t> overwrite;
	const auto entries = SceneSettingsManager::GetSingleton()->GetContextEntries(contextId);
	for (size_t index = 0; index < entries.size(); ++index) {
		const auto& entry = entries[index];
		if (entry.source != SceneSettingsManager::EntrySource::Overwrite || entry.paused || entry.deleted ||
			entry.featureShortName != identity.featureShortName ||
			entry.settingPath != identity.settingPath || entry.settingKey != a_component.settingKey)
			continue;
		// A period past the last slot spans every one of them, as the layer index reads it too.
		if (const auto slot = static_cast<int>(entry.period);
			slot >= 0 && slot < kPeriodCount && !IsCoveredSlot(slot))
			continue;
		overwrite = index;
	}
	return overwrite;
}

std::vector<size_t> SceneWidgetBinding::Guard::CollectOwnedEntries() const
{
	std::vector<size_t> owned;
	for (const auto& component : components)
		for (int slot = 0; slot < kPeriodCount; ++slot)
			if (const auto index = component.periodEntries[static_cast<size_t>(slot)])
				owned.push_back(*index);
	return owned;
}

void SceneWidgetBinding::Guard::ForgetEntries()
{
	for (auto& component : components)
		component.periodEntries = {};
	entryIndex.reset();
	state = State::Absent;
	mixedAcrossPeriods = false;
}

bool SceneWidgetBinding::Guard::EnsureEntries(bool a_deferSave)
{
	auto* manager = SceneSettingsManager::GetSingleton();

	// AddContextSetting snapshots the feature member, so what the control showed before the call has
	// to be back in it: an edit under way would otherwise be recorded as the entry's original, and a
	// shadowed control would capture the value that beat it rather than its own.
	ValueStorage live;
	std::memcpy(live.bytes, value.data, valueSize);
	std::memcpy(value.data, preCall.bytes, valueSize);

	bool added = false;
	ForEachCoveredSlot([&](const Component& a_component, int a_slot, const SceneContextId& a_slotContext) {
		if (a_component.periodEntries[static_cast<size_t>(a_slot)])
			return;
		const auto created = manager->AddContextSetting(a_slotContext, identity.featureShortName,
			identity.settingPath, a_component.settingKey, a_deferSave);
		added = added || created.has_value();
	});

	std::memcpy(value.data, live.bytes, valueSize);

	// Insertion renumbers the entries behind it, so nothing may reuse the indices resolved earlier.
	if (added)
		ResolveState();
	return entryIndex.has_value();
}

void SceneWidgetBinding::Guard::BindDisplayValue()
{
	// A paused entry is not running, and one a layer above shadows never reaches the scene, so in
	// both cases the member carries someone else's value. The control has to show what this page
	// would apply instead of the value that beat it.
	const bool shadowed = upperLayer != SceneSettingsManager::SettingLayer::None && SuppliesValue(winningLayer);
	boundToHolding = state == State::Paused || shadowed;
	if (!boundToHolding)
		return;

	StoreHoldingValue();
	// Entry creation snapshots the member, so a shadowed control's baseline has to be what it showed.
	// A paused one never creates an entry, and its preCall is still the value the scene is running.
	if (shadowed)
		std::memcpy(preCall.bytes, holding.bytes, valueSize);
}

void SceneWidgetBinding::Guard::StoreHoldingValue()
{
	// Components without an entry still read live, so start from the caller's value.
	std::memcpy(holding.bytes, value.data, valueSize);

	const auto entries = SceneSettingsManager::GetSingleton()->GetContextEntries(contextId);
	for (const auto& component : components) {
		const auto index = DisplayEntry(component);
		if (index && *index < entries.size())
			WriteHoldingComponent(component, entries[*index].value);
	}
}

void SceneWidgetBinding::Guard::WriteHoldingComponent(const Component& a_component, const json& a_stored)
{
	if (value.kind == Kind::Bool) {
		if (a_stored.is_boolean())
			*reinterpret_cast<bool*>(holding.bytes) = a_stored.get<bool>();
		else if (a_stored.is_number_integer())
			*reinterpret_cast<bool*>(holding.bytes) = a_stored.get<std::int64_t>() != 0;
		return;
	}
	if (!a_stored.is_number())
		return;

	const auto number = a_stored.get<double>() * widgetScale;
	switch (value.kind) {
	case Kind::Int:
		*reinterpret_cast<int*>(holding.bytes) = static_cast<int>(number);
		break;
	case Kind::Float:
	case Kind::FloatVector:
		reinterpret_cast<float*>(holding.bytes)[a_component.widgetComponent] = static_cast<float>(number);
		break;
	case Kind::Scalar:
		GetScalarTraits(value.scalarType).write(holding.bytes, number);
		break;
	default:
		break;
	}
}

json SceneWidgetBinding::Guard::ReadEditedValue(const Component& a_component) const
{
	// The edit landed wherever the control was bound, which is not the caller's storage once the
	// member was carrying another layer's value.
	const auto* edited = BoundData();

	if (value.kind == Kind::Bool) {
		// A checkbox over an int-backed member (uint flags cast to bool*) persists as an integer, or
		// validation rejects the edit and the entry keeps the base value the scene then re-applies.
		const bool checked = *static_cast<const bool*>(edited);
		return a_component.setting->valueType == SceneSettingsCatalog::ValueType::Integer ?
		           json(static_cast<std::int64_t>(checked)) :
		           json(checked);
	}

	double number = 0.0;
	switch (value.kind) {
	case Kind::Int:
		number = *static_cast<const int*>(edited);
		break;
	case Kind::Float:
	case Kind::FloatVector:
		number = static_cast<const float*>(edited)[a_component.widgetComponent];
		break;
	case Kind::Scalar:
		number = GetScalarTraits(value.scalarType).read(edited);
		break;
	default:
		return {};
	}
	number /= widgetScale;

	// The catalog owns the persisted type, so an integer setting never lands as a float literal.
	return a_component.setting->valueType == SceneSettingsCatalog::ValueType::Integer ?
	           json(static_cast<std::int64_t>(number)) :
	           json(number);
}

std::string SceneWidgetBinding::Guard::DescribeStoredValue() const
{
	const auto entries = SceneSettingsManager::GetSingleton()->GetContextEntries(contextId);

	std::string description;
	for (const auto& component : components) {
		const auto index = PrimaryEntry(component);
		// A tombstone's retained value supplies nothing, so the menu must not offer it as stored.
		if (!index || *index >= entries.size() || entries[*index].deleted)
			continue;
		if (!description.empty())
			description += ", ";
		description += entries[*index].value.dump();
	}
	return description;
}

std::vector<SceneSettingsManager::EntryValueUpdate> SceneWidgetBinding::Guard::BuildEntryValueUpdates() const
{
	std::vector<SceneSettingsManager::EntryValueUpdate> updates;
	for (const auto& component : components) {
		const auto edited = ReadEditedValue(component);
		if (edited.is_null())
			continue;
		for (int slot = 0; slot < kPeriodCount; ++slot)
			if (const auto index = component.periodEntries[static_cast<size_t>(slot)])
				updates.push_back({ *index, edited });
	}
	return updates;
}

std::optional<ImVec4> SceneWidgetBinding::Guard::ResolveProvenanceColor() const
{
	// A disagreement about the data outranks a statement about its source.
	if (mixedAcrossPeriods)
		return Util::Colors::GetWarning();

	// Something resolving after this page wins here, so nothing this page says reaches the scene. Red
	// is kept for the user's own edit shadowing a preset: the one losing pair that is a decision
	// rather than the layering doing its job.
	if (upperLayer != SceneSettingsManager::SettingLayer::None) {
		const bool userShadowsPreset = winningLayer == SceneSettingsManager::SettingLayer::Overwrite &&
		                               upperLayer != SceneSettingsManager::SettingLayer::Overwrite;
		return userShadowsPreset ? Util::Colors::GetError() : Util::Colors::GetWarning();
	}

	// Only this page's own layer is coloured. A layer below is merely what the box happens to show,
	// and this page outranks it, so claiming it owns the address would be over-reporting.
	if (!SuppliesValue(winningLayer))
		return std::nullopt;
	return winningLayer == SceneSettingsManager::SettingLayer::Overwrite ? Util::Colors::GetInfo() :
	                                                                       Util::Colors::GetSuccess();
}

const char* SceneWidgetBinding::Guard::ResolveStatusTooltip() const
{
	if (mixedAcrossPeriods)
		return T(TKEY("scene_override_mixed"),
			"Values differ across this control. Editing writes the same value to all of them.");
	// Whatever wins from above is what the scene actually runs, so it leads over anything else here.
	// Worded for an empty control too: one can be outranked without holding a value of its own.
	if (upperLayer == SceneSettingsManager::SettingLayer::Overwrite)
		return T(TKEY("scene_override_outranked_by_mod"),
			"A mod's value on a scene layer above this one wins here. Anything set on this page only "
			"applies where that one does not.");
	if (upperLayer != SceneSettingsManager::SettingLayer::None)
		return T(TKEY("scene_override_outranked_by_user"),
			"Your override on a scene layer above this one wins here. Anything set on this page only "
			"applies where that one does not.");
	if (lowerLayer == SceneSettingsManager::SettingLayer::Overwrite)
		return T(TKEY("scene_override_from_mod_below"),
			"A mod supplies this value from a scene layer below this one. Tick to pin your own here.");
	if (lowerLayer == SceneSettingsManager::SettingLayer::User)
		return T(TKEY("scene_override_from_user_below"),
			"Your override on a scene layer below this one supplies this value. Tick to pin one here too.");
	switch (state) {
	case State::Overwritten:
		return T(TKEY("scene_override_from_mod"),
			"A mod supplies this value. Tick to pin your own, or remove it to suppress the mod's.");
	case State::Deleted:
		return T(TKEY("scene_override_deleted"),
			"You removed the mod's value here. Tick or edit to take it back.");
	case State::Absent:
		return T(TKEY("scene_override_absent"),
			"No override here. Edit the control, or tick to capture the current value.");
	case State::Paused:
		return T(TKEY("scene_override_paused"), "Override stored but held back. Tick to apply it.");
	default:
		return T(TKEY("scene_override_active"),
			"Override applies here. Untick to hold it back without losing the value.");
	}
}

void SceneWidgetBinding::Guard::Commit()
{
	// Editing a killed value revives it: the tombstone goes first, or it would block the new entry.
	if (state == State::Deleted) {
		SetTombstoned(false);
		ForgetEntries();
	}

	// Nothing will hold the edit, so keeping it would silently rewrite the feature's base.
	if (!HasAllCoveredEntries() && !EnsureEntries(true)) {
		state = State::Unsupported;
		return;
	}

	SceneSettingsManager::GetSingleton()->UpdateContextEntryValues(
		contextId, BuildEntryValueUpdates(), commitDeferred);
}

bool SceneWidgetBinding::Guard::DrawGutter()
{
	bool enabled = state == State::Active;
	const bool hasOverride = state != State::Absent;

	ImGui::PushID(label);

	const auto& style = ImGui::GetStyle();
	auto* menu = Menu::GetSingleton();
	const bool hasRemoveIcon = menu && menu->uiIcons.deleteSettings.texture;
	const float removeIconSize = ImGui::GetFrameHeight() * kRemoveIconScale;
	const float removeWidth = hasRemoveIcon ?
	                              removeIconSize :
	                              ImGui::CalcTextSize(T(TKEY("scene_override_remove"), "Remove")).x +
	                                  style.FramePadding.x * 2.0f;
	gutterConsumedWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + removeWidth;

	// The gutter fills solid where the control only tints: it reads as a state marker, not a hint.
	const auto frameColor = ResolveProvenanceColor();
	if (frameColor) {
		ImGui::PushStyleColor(ImGuiCol_FrameBg, *frameColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, *frameColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, *frameColor);
	}
	const bool toggled = ImGui::Checkbox("##SceneOverride", &enabled);
	if (frameColor)
		ImGui::PopStyleColor(3);

	if (toggled) {
		auto* manager = SceneSettingsManager::GetSingleton();
		// Any gesture on a killed value means the user wants it back, so it captures like an absent one.
		if (state == State::Deleted) {
			SetTombstoned(false);
			ForgetEntries();
		}
		if (state == State::Absent || state == State::Overwritten) {
			// Ticking captures the feature's current value as the override.
			// An aggregate fanned over six periods is 24 entries, so it saves once, not per entry.
			if (EnsureEntries(true))
				manager->SaveAllUserSettings();
		} else {
			// One click normalises a partly paused aggregate instead of inverting each entry.
			for (const auto index : CollectOwnedEntries()) {
				const auto entries = manager->GetContextEntries(contextId);
				if (index < entries.size() && entries[index].paused == enabled)
					manager->TogglePauseContextEntry(contextId, index);
			}
			ResolveState();
		}
	}

	Util::AddTooltip(ResolveStatusTooltip());

	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
	ImGui::BeginDisabled(!hasOverride);
	const bool removeClicked = hasRemoveIcon ?
	                               Util::ErrorImageButton("##SceneOverrideRemove", menu->uiIcons.deleteSettings.texture,
									   ImVec2(removeIconSize, removeIconSize)) :
	                               Util::ErrorTextButton(T(TKEY("scene_override_remove"), "Remove"));
	ImGui::EndDisabled();
	const char* removeTooltip = nullptr;
	if (state == State::Overwritten)
		removeTooltip = T(TKEY("scene_override_remove_mod_tooltip"),
			"Suppress the mod's value here. The mod's own file is left untouched.");
	else if (state == State::Deleted)
		removeTooltip = T(TKEY("scene_override_restore_mod_tooltip"), "Bring the mod's value back here.");
	else
		removeTooltip = T(TKEY("scene_override_remove_tooltip"), "Remove this override from the saved settings.");
	Util::AddTooltip(removeTooltip);
	if (removeClicked)
		DeleteOverride();

	// A duration edit changes no entry's existence or paused flag, so it never restates the row.
	DrawTransitionSlot();

	ImGui::PopID();

	// Hands the row to the control this gutter leads.
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	return toggled || removeClicked;
}

bool SceneWidgetBinding::Guard::SupportsTransition() const
{
	if (contextId.type != SceneSettingsManager::SceneContextType::Location)
		return false;
	// Same predicate FindAllowedCatalogSetting applies when loading a document, so the UI's gate and
	// the loader's gate cannot drift apart.
	return !components.empty() && std::ranges::all_of(components, [](const Component& component) {
		return component.setting && SceneSettingsCatalog::HasFlag(
										component.setting->flags, SceneSettingsCatalog::SettingFlag::Transitionable);
	});
}

void SceneWidgetBinding::Guard::DrawTransitionSlot()
{
	if (contextId.type != SceneSettingsManager::SceneContextType::Location)
		return;

	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	const float width = SceneTransitionField::GetWidth();
	gutterConsumedWidth += ImGui::GetStyle().ItemInnerSpacing.x + width;

	// A non-transitionable row still reserves the slot, or the column would zigzag down the page.
	if (!SupportsTransition()) {
		ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
		return;
	}

	auto* manager = SceneSettingsManager::GetSingleton();
	const auto owned = CollectOwnedEntries();
	// Only an existing entry can carry a duration, so a row without one shows the inherited value inert.
	const bool editable = !owned.empty() && (state == State::Active || state == State::Paused);

	std::optional<float> seconds;
	if (editable) {
		seconds = manager->GetLocationEntryTransitionSeconds(
			contextId.locationType, contextId.locationFormKey, owned.front());
	}

	if (SceneTransitionField::Draw("##SceneTransition", seconds,
			manager->GetLocationTransitionSeconds(), editable)) {
		// One call for every entry the control owns, so an aggregate lands as a single atomic edit.
		manager->SetLocationEntryTransitionSeconds(
			contextId.locationType, contextId.locationFormKey, owned, seconds);
	}

	Util::AddTooltip(editable ?
						 T(TKEY("scene_transition_tooltip"),
							 "Seconds this value takes to ease in and out when the location changes.\n"
							 "Leave the field empty to follow the page's transition time.") :
						 T(TKEY("scene_transition_needs_override_tooltip"),
							 "Enable the override before setting its transition time."),
		Util::kTooltipWhenDisabled);
}

void SceneWidgetBinding::Guard::CaptureNextItemWidth()
{
	auto& nextItem = ImGui::GetCurrentContext()->NextItemData;
	if (!(nextItem.HasFlags & ImGuiNextItemDataFlags_HasWidth))
		return;

	// ItemAdd clears the flag, so the gutter's own checkbox would eat a width meant for the control.
	capturedNextItemWidth = nextItem.Width;
	nextItemWidthCaptured = true;
	nextItem.HasFlags &= ~ImGuiNextItemDataFlags_HasWidth;
}

void SceneWidgetBinding::Guard::PushCompensatedItemWidth()
{
	// CaptureNextItemWidth only runs for a gutter that then draws, and drawing always consumes width.
	assert(!nextItemWidthCaptured || gutterConsumedWidth > 0.0f);
	if (gutterConsumedWidth <= 0.0f)
		return;

	// ImGui derives a default item width from window size rather than cursor position, so a control
	// that keeps its width after a leading gutter runs past the panel's right edge.
	const float consumed = gutterConsumedWidth + ImGui::GetStyle().ItemInnerSpacing.x;
	const float minimum = kMinCompensatedItemWidth * Util::GetUIScale();

	// A width is region-relative when negative, and so measures from the post-gutter cursor: it has
	// already absorbed the gutter and trimming it again would shorten the control twice.
	if (nextItemWidthCaptured) {
		ImGui::SetNextItemWidth(capturedNextItemWidth < 0.0f ?
									capturedNextItemWidth :
									std::max(capturedNextItemWidth - consumed, minimum));
		return;
	}
	if (ImGui::GetCurrentWindow()->DC.ItemWidth < 0.0f)
		return;

	ImGui::PushItemWidth(std::max(ImGui::CalcItemWidth() - consumed, minimum));
	itemWidthPushed = true;
}

void SceneWidgetBinding::Guard::PopCompensatedItemWidth()
{
	if (!itemWidthPushed)
		return;
	ImGui::PopItemWidth();
	itemWidthPushed = false;
}

void SceneWidgetBinding::Guard::DrawContextMenu()
{
	ImGui::PushID(label);
	if (ImGui::BeginPopupContextItem("##SceneOverrideMenu", ImGuiPopupFlags_MouseButtonRight)) {
		auto* manager = SceneSettingsManager::GetSingleton();

		if (const auto stored = DescribeStoredValue(); !stored.empty()) {
			Util::Text::Disabled("%s", stored.c_str());
			ImGui::Separator();
		}

		// A tombstone has no value to revert to and nothing left to delete.
		if (state == State::Deleted) {
			if (ImGui::MenuItem(T(TKEY("scene_override_restore_mod"), "Restore the mod's value")))
				DeleteOverride();
		} else {
			if (ImGui::MenuItem(T(TKEY("scene_override_revert"), "Revert to original"))) {
				for (const auto index : CollectOwnedEntries())
					manager->RevertContextEntryToDefault(contextId, index);
				ResolveState();
			}
			if (ImGui::MenuItem(T(TKEY("scene_override_delete"), "Delete override")))
				DeleteOverride();
		}

		ImGui::EndPopup();
	}
	ImGui::PopID();
}

void SceneWidgetBinding::Guard::DeleteOverride()
{
	// Remove toggles a mod's value between suppressed and restored; its file is never touched.
	if (state == State::Overwritten || state == State::Deleted) {
		SetTombstoned(state == State::Overwritten);
		ResolveState();
		return;
	}

	// Removal renumbers the entries behind it, so drop the highest index first.
	auto owned = CollectOwnedEntries();
	std::ranges::sort(owned, std::greater{});
	for (const auto index : owned)
		SceneSettingsManager::GetSingleton()->RemoveContextSetting(contextId, index);
	ForgetEntries();
}

void SceneWidgetBinding::Guard::SetTombstoned(bool a_tombstoned)
{
	auto* manager = SceneSettingsManager::GetSingleton();

	ForEachCoveredSlot([&](const Component& a_component, int, const SceneContextId& a_slotContext) {
		if (a_tombstoned)
			manager->TombstoneContextSetting(a_slotContext, identity.featureShortName,
				identity.settingPath, a_component.settingKey);
		else
			manager->ClearContextTombstone(a_slotContext, identity.featureShortName,
				identity.settingPath, a_component.settingKey);
	});
}

bool SceneWidgetBinding::Guard::Finish(bool a_changed)
{
	if (tintPushed) {
		Util::PopTintedFrameStyle();
		tintPushed = false;
	}
	if (mixedFlagPushed) {
		ImGui::PopItemFlag();
		mixedFlagPushed = false;
	}
	if (disabledOpened) {
		ImGui::EndDisabled();
		disabledOpened = false;
	}
	PopCompensatedItemWidth();

	if (state == State::Unsupported)
		return a_changed;
	// Both were greyed, so neither took input: no gutter to own and nothing to commit.
	if (state == State::Unbound || state == State::Unavailable)
		return false;

	// Read the drag state before the menu or the gutter becomes the current item.
	const bool dragging = ImGui::IsItemActive();
	const bool settled = ImGui::IsItemDeactivatedAfterEdit();

	if (a_changed && state != State::Paused) {
		commitDeferred = dragging;
		Commit();
	} else if (settled && state == State::Active) {
		// One non-deferred pass so the debounced save always lands on release.
		commitDeferred = false;
		Commit();
	}
	// A drag held still would otherwise outlive the debounce and write the file mid-gesture.
	if (dragging)
		SceneSettingsManager::GetSingleton()->HoldDeferredSceneChanges();

	// Feature code queries the last item after the call, so the control must stay the current one.
	const auto controlItem = ImGui::GetCurrentContext()->LastItemData;
	if (state != State::Unsupported && state != State::Absent && state != State::Overwritten && !dragging)
		DrawContextMenu();
	ImGui::GetCurrentContext()->LastItemData = controlItem;

	// The tint says only that something else holds this value; the gutter's words say what. Drawn
	// before the feature's own tooltip, which appends to the same window once the call returns.
	if (ResolveProvenanceColor())
		Util::AddTooltip(ResolveStatusTooltip(), Util::kTooltipWhenDisabled);

	// A paused control must never report a change: nothing behind it moved.
	return state == State::Paused ? false : a_changed;
}

void SceneWidgetBinding::Guard::OpenDisabled()
{
	ImGui::BeginDisabled();
	disabledOpened = true;
}

#undef I18N_KEY_PREFIX
