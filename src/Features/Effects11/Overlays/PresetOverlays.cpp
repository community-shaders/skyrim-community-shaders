#include "PresetOverlays.h"

#include <algorithm>
#include <cmath>

#include "Features/Effects11/EffectManager.h"
#include "Features/Effects11/IniSerialization.h"
#include "Features/Effects11/PresetManager.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#ifdef ENABLE_ENB_EXTENDER
#	include "Features/Effects11/Effects/ExtendedEffect.h"
#endif

namespace Overlays
{
	namespace
	{
		constexpr const char* kDefaultOverlayName = "NewPresetOverlay";
		constexpr const char* kIniExtension = ".ini";

		/** @brief A #define value as an OverlayValue; null when it is not a plain number (e.g. "1.0f" or an expression). */
		std::optional<OverlayValue> ParseDefine(std::string_view a_type, const std::string& a_value)
		{
			const auto number = ParseFloat(a_value);
			if (!number)
				return std::nullopt;
			OverlayValue value;
			value.kind = a_type == "bool" ? ValueKind::Bool : a_type == "int" ? ValueKind::Int :
			                                                                    ValueKind::Float;
			value.values[0] = *number;
			return value;
		}

		/** @brief A #define value as preprocessor text. */
		std::string FormatDefine(const OverlayValue& a_value)
		{
			switch (a_value.kind) {
			case ValueKind::Bool:
				return a_value.values[0] != 0.0f ? "1" : "0";
			case ValueKind::Int:
				return std::to_string(std::lround(a_value.values[0]));
			default:
				return Effects11Util::Ini::FormatFloat(a_value.values[0]);
			}
		}

		/** @brief The value rounded to the nearest multiple of a_step; unchanged when a_step is not positive. */
		float RoundToStep(float a_value, float a_step)
		{
			return a_step > 0.0f ? std::round(a_value / a_step) * a_step : a_value;
		}

		/** @brief Whether the op is the one written for this slot name. */
		bool SameTarget(const Op& a_op, const SlotName& a_name)
		{
			return Util::IEquals(a_op.category, a_name.category) && MatchKey(a_op.name) == MatchKey(a_name.name) &&
			       Util::IEquals(a_op.fileName, a_name.fileName);
		}
	}

	Manager& Manager::GetSingleton()
	{
		static Manager instance;
		return instance;
	}

	std::filesystem::path Manager::FolderPath() const
	{
		return seriesPath / kFolderName;
	}

	const Overlay* Manager::FindOverlay(std::string_view a_fileName) const
	{
		const auto it = std::find_if(overlays.begin(), overlays.end(), [&](const Overlay& overlay) { return Util::IEquals(overlay.fileName, a_fileName); });
		return it != overlays.end() ? &*it : nullptr;
	}

	Overlay* Manager::FindOverlayMutable(std::string_view a_fileName)
	{
		return const_cast<Overlay*>(FindOverlay(a_fileName));
	}

	int Manager::ActivePosition(std::string_view a_fileName) const
	{
		const auto it = std::find_if(list.active.begin(), list.active.end(), [&](const std::string& name) { return Util::IEquals(name, a_fileName); });
		return it != list.active.end() ? static_cast<int>(it - list.active.begin()) : -1;
	}

	void Manager::LoadFiles()
	{
		const auto newSeries = PresetManager::GetSingleton().GetENBSeriesPath();
		if (newSeries != seriesPath)
			editTarget.clear();
		seriesPath = newSeries;

		stacks.clear();
		applied = false;
		resolver.reset();
		resolved.clear();
		unmapped.clear();
		shaderRows.clear();
		settingRows.clear();
		overlays.clear();
		failedWriteFile.clear();
		failedReadFile.clear();
		++generation;

		std::error_code ec;
		hasFolder = std::filesystem::is_directory(FolderPath(), ec);
		list = LoadList(seriesPath);
		if (hasFolder) {
			for (const auto& entry : std::filesystem::directory_iterator(FolderPath(), ec)) {
				if (entry.is_regular_file(ec) && Util::IEquals(Util::PathHelpers::ToUtf8(entry.path().extension()), kIniExtension))
					overlays.push_back(LoadOverlay(entry.path()));
			}
			std::sort(overlays.begin(), overlays.end(), [](const Overlay& a, const Overlay& b) { return Util::ToLower(a.fileName) < Util::ToLower(b.fileName); });
		}

		std::erase_if(list.active, [&](const std::string& name) {
			const auto* overlay = FindOverlay(name);
			if (overlay && overlay->error.empty())
				return false;
			logger::warn("[Overlays] Active overlay '{}' {}, skipped", name, overlay ? overlay->error : "is missing");
			return true;
		});
		if (!editTarget.empty() && !IsActive(editTarget))
			editTarget.clear();
	}

	std::vector<ResolvedTarget> Manager::ResolveCompatible(const Op& a_op, std::string& a_reason) const
	{
		auto targets = resolver->Resolve(a_op, a_reason);
		std::erase_if(targets, [&](const ResolvedTarget& target) { return !IsCompatible(SliceOperand(a_op, target.operandComponent), ReadSlot(target.slot).value); });
		if (targets.empty() && a_reason.empty())
			a_reason = "operand does not fit the value";
		return targets;
	}

	void Manager::ResolveOverlay(size_t a_overlay)
	{
		auto& overlay = overlays[a_overlay];
		auto& targets = resolved[a_overlay];
		targets.clear();
		unmapped[a_overlay] = 0;
		for (const auto& op : overlay.ops) {
			std::string reason;
			targets.push_back(ResolveCompatible(op, reason));
			if (targets.back().empty()) {
				++unmapped[a_overlay];
				logger::debug("[Overlays] {} [{}] {}: {}", overlay.fileName, op.category, op.name, reason);
			}
		}
	}

	void Manager::OnStoresLoaded()
	{
		stacks.clear();  // the stores hold fresh file values, so there is nothing to restore
		applied = false;
		RebuildWeatherIndex();
		resolver.emplace();
		resolved.assign(overlays.size(), {});
		unmapped.assign(overlays.size(), 0);

		size_t opCount = 0, unmappedCount = 0;
		for (size_t i = 0; i < overlays.size(); ++i) {
			ResolveOverlay(i);
			opCount += overlays[i].ops.size();
			unmappedCount += unmapped[i];
			if (unmapped[i])
				logger::info("[Overlays] '{}': {} of {} parameters are not used by Effects 11", overlays[i].fileName, unmapped[i], overlays[i].ops.size());
		}
		if (!overlays.empty())
			logger::info("[Overlays] {} overlays, {} ops, {} not used by Effects 11, {} active", overlays.size(), opCount, unmappedCount, list.active.size());

		Reapply();
	}

	void Manager::ApplyOverlay(size_t a_overlay)
	{
		const auto& overlay = overlays[a_overlay];
		// A zero amount changes nothing, so it must not define weather slots that later overlays would inherit into
		if (overlay.amount == 0.0f)
			return;
		for (size_t opIndex = 0; opIndex < overlay.ops.size(); ++opIndex) {
			for (const auto& target : resolved[a_overlay][opIndex]) {
				auto [it, inserted] = stacks.try_emplace(target.slot);
				auto& stack = it->second;
				if (inserted) {
					stack.original = ReadSlot(target.slot);
					stack.live = stack.original;
				}
				if (!stack.live.defined) {
					// A wildcard leaves an undefined weather period to inherit the changed base value
					if (!target.explicitWeather) {
						if (inserted)
							stacks.erase(it);
						continue;
					}
					// Resolve emits base targets first, so a base stack already holds this pass's live value
					const auto base = stacks.find(target.slot.Base());
					stack.live = { base != stacks.end() ? base->second.live.value : ReadSlot(target.slot.Base()).value, true };
				}
				const auto before = stack.live.value;
				stack.live.value = ApplyOperation(before, SliceOperand(overlay.ops[opIndex], target.operandComponent), overlay.amount);
				stack.ops.push_back({ a_overlay, opIndex, before, stack.live.value });
			}
		}
	}

	void Manager::RestoreOriginals()
	{
		for (const auto& [slot, stack] : stacks)
			WriteSlot(slot, stack.original);
		applied = false;
	}

	void Manager::RestoreBeforeReload()
	{
		RestoreOriginals();
		FlushWeatherWrites();
		stacks.clear();
	}

	void Manager::WriteLive()
	{
		for (const auto& [slot, stack] : stacks)
			WriteSlot(slot, stack.live);
		applied = true;
	}

	void Manager::Reapply()
	{
		RestoreOriginals();
		stacks.clear();
		if (resolver) {
			for (const auto& fileName : list.active) {
				if (const auto* overlay = FindOverlay(fileName))
					ApplyOverlay(static_cast<size_t>(overlay - overlays.data()));
			}
		}
		// Clamped once after the whole stack, where the loader clamps
		for (auto& [slot, stack] : stacks)
			stack.live.value = ClampSlot(slot, stack.live.value);
		WriteLive();
		FlushWeatherWrites();

		const auto defineOps = CollectDefineOps();
		shaderReloadNeeded = std::any_of(compiledDefines.begin(), compiledDefines.end(), [&](const CompiledDefine& define) { return CompileDefine(define, defineOps) != define.compiledValue; });
		RebuildRowInfo();
	}

	Manager::RemovedScope::RemovedScope()
	{
		auto& manager = GetSingleton();
		restored = manager.applied;
		if (restored) {
			manager.RestoreOriginals();
			FlushWeatherWrites();
		}
	}

	Manager::RemovedScope::~RemovedScope()
	{
		if (restored) {
			GetSingleton().WriteLive();
			FlushWeatherWrites();
		}
	}

	std::vector<Manager::DefineOp> Manager::CollectDefineOps() const
	{
		std::vector<DefineOp> defineOps;
		for (const auto& fileName : list.active) {
			const auto* overlay = FindOverlay(fileName);
			assert(overlay && "LoadFiles drops missing overlays from Active");
			for (const auto& op : overlay->ops) {
				if (OpTargetsBaseFile(op))
					defineOps.push_back({ &op, overlay->amount, MatchKey(op.name) });
			}
		}
		return defineOps;
	}

	std::string Manager::CompileDefine(const CompiledDefine& a_define, const std::vector<DefineOp>& a_ops)
	{
		std::optional<OverlayValue> value;
		for (const auto& defineOp : a_ops) {
			if (defineOp.key != a_define.key || !Util::IEquals(defineOp.op->category, a_define.section))
				continue;
			if (!value) {
				value = ParseDefine(a_define.type, a_define.iniValue);
				if (!value)
					return a_define.iniValue;
			}
			if (IsCompatible(*defineOp.op, *value))
				value = ApplyOperation(*value, *defineOp.op, defineOp.amount);
		}
		return value ? FormatDefine(*value) : a_define.iniValue;
	}

	void Manager::TransformDefine(std::string_view a_section, std::string_view a_key, std::string_view a_type, std::string& a_value)
	{
		CompiledDefine define{ std::string(a_section), MatchKey(a_key), std::string(a_type), a_value, {} };
		define.compiledValue = CompileDefine(define, CollectDefineOps());
		a_value = define.compiledValue;
		compiledDefines.push_back(std::move(define));
	}

	void Manager::RebuildRowInfo()
	{
		shaderRows.clear();
		settingRows.clear();
		const auto* target = editTarget.empty() ? nullptr : FindOverlay(editTarget);
		allRowsLocked = target && target->amount <= 0.0f;
		std::vector<int> activePositions(overlays.size(), -1);
		for (size_t position = 0; position < list.active.size(); ++position) {
			if (const auto* overlay = FindOverlay(list.active[position]))
				activePositions[static_cast<size_t>(overlay - overlays.data())] = static_cast<int>(position);
		}
		const int targetPosition = target ? activePositions[static_cast<size_t>(target - overlays.data())] : -1;
		for (const auto& [slot, stack] : stacks) {
			auto& row = slot.IsShader() ? shaderRows[{ slot.effect, slot.index }] : settingRows[slot.index];
			for (const auto& op : stack.ops) {
				const bool known = std::any_of(row.contributions.begin(), row.contributions.end(), [&](const Contribution& c) { return c.overlay == op.overlay; });
				if (!known)
					row.contributions.push_back({ op.overlay, op.before, op.after });
				if (targetPosition < 0 || activePositions[op.overlay] > targetPosition)
					row.locked = true;
			}
		}
	}

	const RowInfo* Manager::GetShaderRow(const Effect& a_effect, size_t a_index) const
	{
		const auto it = shaderRows.find({ &a_effect, a_index });
		return it != shaderRows.end() ? &it->second : nullptr;
	}

	const RowInfo* Manager::GetSettingRow(uint32_t a_settingID) const
	{
		const auto it = settingRows.find(a_settingID);
		return it != settingRows.end() ? &it->second : nullptr;
	}

	bool Manager::TrackWrite(std::string_view a_fileName, bool a_written)
	{
		if (!a_written) {
			failedWriteFile = a_fileName;
			return false;
		}
		for (auto* failedFile : { &failedWriteFile, &failedReadFile })
			if (Util::IEquals(*failedFile, a_fileName))
				failedFile->clear();
		return true;
	}

	void Manager::SaveListFile()
	{
		TrackWrite(kListFileName, SaveList(seriesPath, list));
	}

	bool Manager::SaveOverlayFile(Overlay& a_overlay)
	{
		if (!TrackWrite(a_overlay.fileName, SaveOverlay(FolderPath() / Util::PathHelpers::FromUtf8(a_overlay.fileName), a_overlay)))
			return false;
		a_overlay.dirty = false;
		return true;
	}

	void Manager::SetEnabled(std::string_view a_fileName, bool a_enabled)
	{
		const auto* overlay = FindOverlay(a_fileName);
		if (!overlay || (a_enabled && !overlay->error.empty()))
			return;
		std::erase_if(list.active, [&](const std::string& name) { return Util::IEquals(name, a_fileName); });
		if (a_enabled) {
			for (const auto& other : overlay->incompatible)
				std::erase_if(list.active, [&](const std::string& name) { return Util::IEquals(name, other); });
			list.active.push_back(overlay->fileName);
		}
		if (!editTarget.empty() && !IsActive(editTarget))
			editTarget.clear();
		SaveListFile();
		Reapply();
	}

	void Manager::Move(std::string_view a_fileName, int a_delta)
	{
		const int from = ActivePosition(a_fileName);
		const int to = from + a_delta;
		if (from < 0 || to < 0 || to >= static_cast<int>(list.active.size()))
			return;
		std::swap(list.active[from], list.active[to]);
		SaveListFile();
		Reapply();
	}

	void Manager::SetAmount(std::string_view a_fileName, float a_amount)
	{
		auto* overlay = FindOverlayMutable(a_fileName);
		if (!overlay)
			return;
		const float snapped = overlay->amountMin + RoundToStep(a_amount - overlay->amountMin, overlay->amountStep);
		overlay->amount = std::clamp(snapped, overlay->amountMin, overlay->amountMax);
		Reapply();
	}

	void Manager::SaveAmount(std::string_view a_fileName)
	{
		auto* overlay = FindOverlayMutable(a_fileName);
		if (!overlay)
			return;
		if (!overlay->dirty) {
			SaveOverlayFile(*overlay);
			return;
		}
		// Authored ops wait for Save, so only the Amount goes into the file as it is on disk
		auto onDisk = LoadOverlay(FolderPath() / Util::PathHelpers::FromUtf8(overlay->fileName));
		onDisk.amount = overlay->amount;
		if (onDisk.error.empty()) {
			SaveOverlayFile(onDisk);
			return;
		}
		logger::warn("[Overlays] {}: Amount not saved, {}", overlay->fileName, onDisk.error);
		failedReadFile = overlay->fileName;
	}

	void Manager::SetEditTarget(std::string a_fileName)
	{
		if (!a_fileName.empty() && !IsActive(a_fileName))
			SetEnabled(a_fileName, true);
		editTarget = IsActive(a_fileName) ? std::move(a_fileName) : std::string();
		RebuildRowInfo();
	}

	bool Manager::HasDirtyOverlays() const
	{
		return std::any_of(overlays.begin(), overlays.end(), [](const Overlay& overlay) { return overlay.dirty; });
	}

	bool Manager::SaveDirtyOverlays()
	{
		bool ok = true;
		for (auto& overlay : overlays)
			if (overlay.dirty)
				ok &= SaveOverlayFile(overlay);
		return ok;
	}

	void Manager::RecordEdit(const SlotKey& a_slot, const OverlayValue& a_before, const OverlayValue& a_after)
	{
		auto* target = FindOverlayMutable(editTarget);
		assert(target && target->amount > 0.0f);
		const size_t targetIndex = static_cast<size_t>(target - overlays.data());
		const auto name = DescribeSlot(a_slot);
		const auto existing = std::find_if(target->ops.begin(), target->ops.end(), [&](const Op& op) { return SameTarget(op, name); });

		const size_t opIndex = static_cast<size_t>(existing - target->ops.begin());

		// An existing op is rewritten against the value it receives; a new one is appended last, so it receives the pre-edit value
		OverlayValue received = a_before;
		if (existing != target->ops.end()) {
			if (const auto stack = stacks.find(a_slot); stack != stacks.end()) {
				const auto step = std::find_if(stack->second.ops.begin(), stack->second.ops.end(), [&](const StackOp& stackOp) { return stackOp.overlay == targetIndex && stackOp.op == opIndex; });
				if (step != stack->second.ops.end())
					received = step->before;
			}
		}

		Op op;
		op.fileName = name.fileName;
		op.category = name.category;
		op.name = name.name;
		bool noOp = true;
		if (a_after.kind == ValueKind::Float) {
			op.op = Operator::Add;
			op.operand.components = a_after.components;
			for (int c = 0; c < a_after.components; ++c) {
				const float delta = a_after.values[c] - received.values[c];
				op.operand.values[c] = delta / target->amount;
				noOp &= delta == 0.0f;
			}
		} else {
			op.op = Operator::Set;
			op.operand = a_after;
			op.operand.components = 1;
			noOp = a_after == received;
		}

		if (existing != target->ops.end()) {
			if (noOp) {
				target->ops.erase(existing);
				resolved[targetIndex].erase(resolved[targetIndex].begin() + opIndex);
				ForgetStackOp(targetIndex, opIndex);
			} else {
				*existing = op;
				std::string reason;
				resolved[targetIndex][opIndex] = ResolveCompatible(op, reason);
			}
		} else if (!noOp) {
			std::string reason;
			resolved[targetIndex].push_back(ResolveCompatible(op, reason));
			target->ops.push_back(std::move(op));
		}
		target->dirty = true;
	}

	void Manager::ForgetStackOp(size_t a_overlay, size_t a_op)
	{
		for (auto& [slot, stack] : stacks) {
			std::erase_if(stack.ops, [&](const StackOp& stackOp) { return stackOp.overlay == a_overlay && stackOp.op == a_op; });
			for (auto& stackOp : stack.ops)
				if (stackOp.overlay == a_overlay && stackOp.op > a_op)
					--stackOp.op;
		}
	}

	bool Manager::TakeShaderEdit(Effect& a_effect, size_t a_index, const OverlayValue& a_before)
	{
		if (editTarget.empty())
			return false;
		auto& var = a_effect.uiVariables[a_index];
		const auto after = ReadVariableValue(var, false);
		WriteVariableValue(var, a_before, false);  // undo the widget's write; the overlay re-applies it

		std::string weatherFile;
#ifdef ENABLE_ENB_EXTENDER
		if (const auto* entry = static_cast<ExtendedEffect&>(a_effect).WeatherEntryForEdit(a_index, EffectManager::GetSingleton().GetDominantWeatherID()))
			weatherFile = entry->fileName;
#endif
		// A weather slot's own value, not the displayed blend in a_before, is what the op changes
		const SlotKey whole{ &a_effect, static_cast<uint32_t>(a_index), -1, weatherFile };
		if (Effect::IsPerComponentVector(var)) {
			for (int c = 0; c < after.components; ++c) {
				if (after.values[c] == a_before.values[c])
					continue;
				auto slot = whole;
				slot.part = static_cast<int8_t>(c);
				RecordEdit(slot, ReadSlot(slot).value, ComponentOf(after, c));
			}
		} else {
			RecordEdit(whole, ReadSlot(whole).value, after);
		}
		Reapply();
		return true;
	}

	bool Manager::TakeSettingEdit(uint32_t a_settingID, const std::string& a_weatherFile, const SettingValue& a_before, const SettingValue& a_after)
	{
		if (editTarget.empty())
			return false;
		ForEachSettingPart(*SettingManager::GetSingleton().GetSettingInfo(a_settingID), [&](int8_t a_part) {
			const auto before = SettingPartValue(a_before, a_part);
			const auto after = SettingPartValue(a_after, a_part);
			if (!(before == after))
				RecordEdit({ nullptr, a_settingID, a_part, a_weatherFile }, before, after);
		});
		Reapply();
		return true;
	}

	bool Manager::CreateOverlay(std::string_view a_name)
	{
		std::error_code ec;
		std::filesystem::create_directories(FolderPath(), ec);
		auto stem = Util::FileHelpers::SanitizeFileName(std::string(a_name));
		if (stem.empty())
			stem = kDefaultOverlayName;
		std::string fileName = stem + kIniExtension;
		for (int suffix = 2; FindOverlay(fileName) || std::filesystem::exists(FolderPath() / Util::PathHelpers::FromUtf8(fileName), ec); ++suffix)
			fileName = std::format("{}{}{}", stem, suffix, kIniExtension);

		Overlay overlay;
		overlay.fileName = fileName;
		overlay.uiName = std::string(a_name);
		if (!SaveOverlayFile(overlay))
			return false;
		overlays.push_back(std::move(overlay));
		resolved.emplace_back();
		unmapped.push_back(0);
		hasFolder = true;
		++generation;
		SetEditTarget(fileName);
		return true;
	}

	bool Manager::CreateOverlayFromChanges(std::string_view a_name, const std::function<void()>& a_revert)
	{
		// Files are created and saved before the revert, so a failed write keeps the unsaved edits
		if (!SaveDirtyOverlays())
			return false;
		const auto slots = EnumerateSlots();
		std::vector<SlotValue> edited;
		edited.reserve(slots.size());
		for (const auto& slot : slots)
			edited.push_back(ReadSlot(slot));
		if (!CreateOverlay(a_name))
			return false;

		const std::string fileName = editTarget;
		a_revert();
		SetEditTarget(fileName);  // the reload keeps it active only when the list write succeeded
		if (editTarget.empty()) {
			// The reloaded overlay cannot take the edits, so they go back into the base preset
			for (size_t i = 0; i < slots.size(); ++i) {
				const auto now = ReadSlot(slots[i]);
				if (edited[i].defined && !(now.defined && now.value == edited[i].value))
					WriteSlot(slots[i], edited[i]);
			}
			FlushWeatherWrites();
			return false;
		}

		// Undefined weather slots inherit the base value, so they are recorded once the base ops apply
		for (const bool weatherPass : { false, true }) {
			for (size_t i = 0; i < slots.size(); ++i) {
				if (!edited[i].defined || slots[i].weatherFile.empty() == weatherPass)
					continue;
				const auto now = ReadSlot(slots[i]);
				const auto received = now.defined ? now.value : ReadSlot(slots[i].Base()).value;
				if (!(received == edited[i].value))
					RecordEdit(slots[i], received, edited[i].value);
			}
			Reapply();
		}
		auto* overlay = FindOverlayMutable(editTarget);
		return overlay && SaveOverlayFile(*overlay);
	}

	bool Manager::SavePreset()
	{
		bool baseSaved;
		{
			const RemovedScope removed;
			const bool settingsSaved = SettingManager::GetSingleton().Save();
			baseSaved = EffectManager::GetSingleton().Save() && settingsSaved;
		}
		return SaveDirtyOverlays() && baseSaved;
	}
}
