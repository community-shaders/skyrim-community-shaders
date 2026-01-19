#pragma once

#include "PCH.h"

namespace RE
{
	struct CellAttachDetachEvent
	{
		enum Status : std::uint32_t
		{
			StartAttach = 0,
			FinishAttach = 1,
			StartDetach = 2,
			FinishDetach = 3
		};

		RE::TESObjectCELL* cell;  // 00
		Status status;            // 08  0 start attach 1 finish attach 2 start detach 3 finish detach
	};
}