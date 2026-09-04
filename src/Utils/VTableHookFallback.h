#pragma once

namespace Util
{
	/**
	 * @brief Hook virtual slot a_idx of a_object with a_thunk when Detours could not patch
	 * the function in that slot.
	 *
	 * First writes a_thunk into the vtable slot in place. When the vtable page rejects
	 * VirtualProtect, points the object at a patched clone of its vtable instead.
	 *
	 * Under Wine/CrossOver the D3D11 translation layer lives in host-mapped memory that
	 * rejects every protection change, so only the clone can carry the hook.
	 *
	 * @return The original function pointer that the hook must call, or 0 when the slot
	 * was left unhooked.
	 */
	std::uintptr_t VTableHookFallback(void* a_object, std::size_t a_idx, void* a_thunk, LONG a_detourError);
}
