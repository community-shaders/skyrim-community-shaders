#pragma once

namespace Util
{
	/**
	 * @brief Hooks virtual slot a_idx of COM object a_object with a_thunk after Detours
	 * failed to patch the slot's implementation: swaps the vtable slot in place, or, when
	 * the vtable page itself rejects VirtualProtect, repoints the object at a patched
	 * clone of its vtable.
	 *
	 * Needed under Wine/CrossOver, where the D3D11 translation layer lives in host-mapped
	 * memory whose pages reject every protection change, so only the clone can carry the
	 * hook.
	 *
	 * @return The original function pointer the hook must call through.
	 */
	std::uintptr_t VTableHookFallback(void* a_object, std::size_t a_idx, void* a_thunk, LONG a_detourError);
}
