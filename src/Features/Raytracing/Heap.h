#pragma once

typedef unsigned int uint32_t;

template <typename TableEnum, typename SlotEnum>
struct Heap
{
    using Table = TableEnum;
    using Slot  = SlotEnum;

	static uint32_t GetTableValue(Table table)
	{
		return static_cast<uint32_t>(table);
	}

	static uint32_t GetSlotValue(Slot slot)
	{
		return static_cast<uint32_t>(slot);
	}

	static uint32_t NumDescriptors()
	{
		return static_cast<uint32_t>(Slot::NumDescriptors);
	}
};

template <typename T>
concept IsHeap = requires(T t, typename T::Table table, typename T::Slot slot) {
	{ t.GetTableValue(table) } -> std::convertible_to<uint32_t>;
	{ t.GetSlotValue(slot) } -> std::convertible_to<uint32_t>;
};
