class Allocator
{
public:
	explicit Allocator(uint32_t maxSlots) :
		nextFree(0), slots(maxSlots)
	{
		// Initialize free list
		for (uint32_t i = 0; i < maxSlots - 1; ++i)
			slots[i] = i + 1;
		slots[maxSlots - 1] = INVALID;  // end of list
	}

	// Allocate the lowest available slot (O(1))
	uint32_t allocate()
	{
		assert(nextFree != INVALID && "No available slots!");
		uint32_t slot = nextFree;
		nextFree = slots[slot];  // move head
		return slot;
	}

	// Free a slot (O(1))
	void free(uint32_t slot)
	{
		slots[slot] = nextFree;
		nextFree = slot;
	}

	bool hasFree() const { return nextFree != INVALID; }

private:
	static constexpr uint32_t INVALID = 0xFFFFFFFF;
	uint32_t nextFree;
	eastl::vector<uint32_t> slots;
};