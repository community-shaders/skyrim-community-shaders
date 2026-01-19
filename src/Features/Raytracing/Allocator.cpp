#include "Features/Raytracing/Allocator.h"

void Allocation::FreeAllocation() const
{
	logger::debug("[RT] Allocation::FreeAllocation - Index {}", index);
	allocator->Free(index);
}