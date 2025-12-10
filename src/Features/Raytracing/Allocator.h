#pragma once

class Allocator;

class Allocation
{
	uint16_t index;
	Allocator* allocator;

public:
	Allocation(uint16_t slot, Allocator* allocator) :
		index(slot), allocator(allocator) {}

    Allocation(const Allocation&) = delete;
	Allocation& operator=(const Allocation&) = delete;

	Allocation(Allocation&&) = default;
	Allocation& operator=(Allocation&&) = default;

	uint16_t GetIndex() const
	{
		return index;
	}

	void FreeAllocation() const;
};

class Allocator
{
public:
	explicit Allocator(uint16_t maxSlots) :
		nextFree(0), slots(maxSlots)
	{
		// Initialize free list
		for (uint16_t i = 0; i < maxSlots - 1; ++i)
			slots[i] = i + 1;

		slots[maxSlots - 1] = INVALID;  // end of list
	}

	// Allocate the lowest available slot
	Allocation* Allocate()
	{
		assert(nextFree != INVALID && "No available slots!");
		uint16_t slot = nextFree;
		nextFree = slots[slot];  // move head
		return new Allocation(slot, this);
	}

	// Free a slot
	void Free(uint16_t slot)
	{
		slots[slot] = nextFree;
		nextFree = slot;
	}

	bool HasFree() const { return nextFree != INVALID; }

	uint16_t FreeCount() const
	{
		uint16_t count = 0;
		uint16_t cur = nextFree;

		while (cur != INVALID) {
			++count;
			cur = slots[cur];
		}
		return count;
	}

	uint16_t UsedCount() const
	{
		return static_cast<uint16_t>(slots.size() - FreeCount());
	}

private:
	static constexpr uint16_t INVALID = 0xFFFF;
	uint16_t nextFree;
	eastl::vector<uint16_t> slots;
};

struct AllocationDeleter
{
	void operator()(Allocation* a) const noexcept
	{
		a->FreeAllocation();
		delete a;
	}
};