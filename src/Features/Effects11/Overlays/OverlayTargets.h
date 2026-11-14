#pragma once

#include <compare>
#include <string>
#include <unordered_map>
#include <vector>

#include "Features/Effects11/Effects/Effect.h"
#include "Features/Effects11/SettingManager.h"
#include "OverlayFiles.h"

class ExtendedEffect;

namespace Overlays
{
	/** @brief One stored value an op can change: a setting or shader variable, optionally one period or component, in the base file or one weather file. */
	struct SlotKey
	{
		Effect* effect = nullptr;  ///< Null for settings
		uint32_t index = 0;        ///< Setting ID, or index into effect->uiVariables
		int8_t part = -1;          ///< Time-of-day period of a setting or component of a per-component vector; -1 for the whole value
		std::string weatherFile;   ///< Empty for the base file, else a WeatherEntry::fileName

		auto operator<=>(const SlotKey&) const = default;
		bool IsShader() const { return effect != nullptr; }
		SlotKey Base() const
		{
			SlotKey base = *this;
			base.weatherFile.clear();
			return base;
		}
	};

	/** @brief A slot's value; a weather slot is undefined when its file does not set it. */
	struct SlotValue
	{
		OverlayValue value;
		bool defined = true;
	};

	/** @brief Where an op lands: the slot and which operand component applies (-1 for the whole operand). */
	struct ResolvedTarget
	{
		SlotKey slot;
		int operandComponent = -1;
		bool explicitWeather = false;  ///< Named by file, so an undefined weather period is defined from the base value
	};

	/** @brief FileName, Category and Name an op on this slot is written with. */
	struct SlotName
	{
		std::string fileName;
		std::string category;
		std::string name;
	};

	/** @brief Re-indexes weather file names to their weather IDs. Call once per store load, before resolving; reads and writes of weather slots use it. */
	void RebuildWeatherIndex();
	/** @brief A setting value as an OverlayValue; a_part picks one period of a time-of-day value. */
	OverlayValue SettingPartValue(const SettingValue& a_value, int a_part);
	/** @brief Whether an op changes its category's base file: no FileName, or one naming or matching the base file. */
	bool OpTargetsBaseFile(const Op& a_op);
	/** @brief An ini key as ops match it: normalized and lowercased, since the Win32 profile API ignores case. */
	std::string MatchKey(std::string_view a_name);
	/** @brief Whether a setting holds one value per time-of-day period. */
	bool HasPeriods(const Setting& a_setting);

	/** @brief Calls a_visit with each slot part of a setting: every period, or -1 for a single value. */
	template <typename Visit>
	void ForEachSettingPart(const Setting& a_setting, Visit&& a_visit)
	{
		const bool periods = HasPeriods(a_setting);
		for (int part = periods ? 0 : -1; part < (periods ? TimeOfDayValue::Total : 0); ++part)
			a_visit(static_cast<int8_t>(part));
	}

	/** @brief A slot's current value, as the stores hold it. */
	SlotValue ReadSlot(const SlotKey& a_slot);
	/** @brief Writes a slot; weather writes are batched until FlushWeatherWrites. */
	void WriteSlot(const SlotKey& a_slot, const SlotValue& a_value);
	/** @brief Re-parses the weather caches of effects whose weather slots were written. */
	void FlushWeatherWrites();
	/** @brief Clamps setting slots to their registered range, as loading does; shader slots are returned unchanged. */
	OverlayValue ClampSlot(const SlotKey& a_slot, const OverlayValue& a_value);
	/** @brief The FileName, Category and Name an authored op on this slot is written with. */
	SlotName DescribeSlot(const SlotKey& a_slot);
	/** @brief Every slot that holds a value, for "New overlay from unsaved changes". */
	std::vector<SlotKey> EnumerateSlots();

	/** @brief Live value of a shader variable; a_base reads the base value of a weather-separated variable. */
	OverlayValue ReadVariableValue(const Effect::UIVariable& a_var, bool a_base);
	/** @brief Writes a shader variable and pushes it to the effect; a_alsoBase also sets the base value. */
	void WriteVariableValue(Effect::UIVariable& a_var, const OverlayValue& a_value, bool a_alsoBase);
	/** @brief One component of a value as a single-component value. */
	OverlayValue ComponentOf(const OverlayValue& a_value, int a_component);
	/** @brief The op with only one operand component, or unchanged for -1. */
	Op SliceOperand(const Op& a_op, int a_component);

	/** @brief Resolves ops to slots. Build once per store load: it indexes every shader parameter. */
	class Resolver
	{
	public:
		Resolver();
		/** @param a_reason Receives why nothing matched. */
		std::vector<ResolvedTarget> Resolve(const Op& a_op, std::string& a_reason) const;

	private:
		struct EffectIndex
		{
			Effect* effect = nullptr;
			std::unordered_map<std::string, uint32_t> keys;  ///< MatchKey of the ini key -> variable index
		};
		struct BaseTarget
		{
			SlotKey slot;
			int operandComponent = -1;
			bool weatherCapable = false;
		};

		bool ResolveShader(const Op& a_op, std::vector<BaseTarget>& a_out, std::string& a_reason) const;
		bool ResolveSetting(const Op& a_op, std::vector<BaseTarget>& a_out, std::string& a_reason) const;
		std::vector<std::string> MatchWeatherFiles(std::string_view a_fileName, bool a_wildcard) const;

		std::vector<EffectIndex> effects;
		std::vector<std::string> weatherFiles;
	};
}
