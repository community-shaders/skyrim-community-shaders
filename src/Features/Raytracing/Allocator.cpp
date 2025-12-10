#include "Features/Raytracing/Allocator.h"

void Allocation::FreeAllocation() const
{
	logger::info("[RT] Allocation::FreeAllocation - Index {}", index);
	allocator->Free(index);
}