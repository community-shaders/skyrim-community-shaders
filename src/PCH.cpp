#include "PCH.h"

void* operator new[](size_t size, const char*, int, unsigned, const char*, int)
{
	return new uint8_t[size];
}

void* operator new[](size_t size, size_t, size_t, const char*, int,
	unsigned, const char*, int)
{
	return new uint8_t[size];
}
