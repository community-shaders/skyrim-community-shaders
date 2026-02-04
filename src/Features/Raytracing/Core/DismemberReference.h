#pragma once

#include "PCH.h"

#include "Features/Raytracing/Core/Shape.h"

struct DismemberReference
{
public:
	Shape* shape;
	std::uint16_t slot;
};