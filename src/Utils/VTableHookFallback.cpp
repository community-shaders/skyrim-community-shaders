#include "VTableHookFallback.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
	inline constexpr std::size_t max_cloned_vtable_slots = 256;

	// Objects repointed at a plugin-owned vtable clone. Later hooks on the same object
	// must patch the same clone, and clones must stay dispatchable after this DLL's
	// static destructors have run, so the map is intentionally leaked.
	std::mutex clonedVTableMutex;
	auto& clonedVTables = *new std::unordered_map<void*, std::unique_ptr<std::uintptr_t[]>>();

	// Copies vtable slots page by page, stopping at the first page that faults; the
	// vtable may live in memory VirtualQuery cannot measure. Returns the slots copied.
	std::size_t CopyReadableSlots(std::uintptr_t* a_dst, const std::uintptr_t* a_src, std::size_t a_count) noexcept
	{
		std::size_t copied = 0;
		__try {
			while (copied < a_count) {
				const auto cursor = reinterpret_cast<std::uintptr_t>(a_src + copied);
				const auto pageEnd = (cursor & ~static_cast<std::uintptr_t>(0xFFF)) + 0x1000;
				const auto chunk = std::min(a_count - copied, (pageEnd - cursor) / sizeof(std::uintptr_t));
				std::memcpy(a_dst + copied, a_src + copied, chunk * sizeof(std::uintptr_t));
				copied += chunk;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
		return copied;
	}
}

namespace Util
{
	std::uintptr_t VTableHookFallback(void* a_object, std::size_t a_idx, void* a_thunk, LONG a_detourError)
	{
		std::scoped_lock lock(clonedVTableMutex);

		// Read under the lock: a concurrent hook may have just repointed this object at a
		// clone, and the clone check below must see the current vtable pointer.
		auto vtable = *static_cast<std::uintptr_t**>(a_object);
		const auto original = vtable[a_idx];

		if (a_idx >= max_cloned_vtable_slots) {
			logger::warn("[Hooks] virtual slot {} exceeds the supported clone size {}; left unhooked (Detours error {})", a_idx, max_cloned_vtable_slots, a_detourError);
			return original;
		}

		// An earlier hook may already have repointed this object at a clone; cloning again
		// would discard it. The slot value read above came from the clone, so returning it
		// preserves chaining.
		if (auto it = clonedVTables.find(a_object); it != clonedVTables.end() && it->second.get() == vtable) {
			it->second[a_idx] = reinterpret_cast<std::uintptr_t>(a_thunk);
			logger::warn("[Hooks] Detours could not patch virtual slot {} (error {}); patched the object's existing vtable clone", a_idx, a_detourError);
			return original;
		}

		MEMORY_BASIC_INFORMATION mbi{};
		const bool queried = VirtualQuery(vtable, &mbi, sizeof(mbi)) == sizeof(mbi);

		// Swap the slot in place, keeping execute rights if the page has them.
		constexpr DWORD executeProtects = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		const DWORD writableProtect = (queried && (mbi.Protect & executeProtects) != 0) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
		DWORD previousProtect = 0;
		if (VirtualProtect(&vtable[a_idx], sizeof(std::uintptr_t), writableProtect, &previousProtect)) {
			vtable[a_idx] = reinterpret_cast<std::uintptr_t>(a_thunk);
			DWORD restoredProtect = 0;
			if (!VirtualProtect(&vtable[a_idx], sizeof(std::uintptr_t), previousProtect, &restoredProtect))
				logger::warn("[Hooks] could not restore protection {:#x} on virtual slot {} (error {})", previousProtect, a_idx, GetLastError());
			logger::warn("[Hooks] Detours could not patch virtual slot {} (error {}); swapped the vtable slot in place", a_idx, a_detourError);
			return original;
		}
		const DWORD protectResult = GetLastError();

		// Clone the vtable, patch the copy, repoint the object; needs no protection change.
		// Copy the full capacity so every readable source slot keeps resolving through
		// the clone.
		auto clone = std::make_unique<std::uintptr_t[]>(max_cloned_vtable_slots);
		if (CopyReadableSlots(clone.get(), vtable, max_cloned_vtable_slots) <= a_idx) {
			logger::warn("[Hooks] virtual slot {} could not be hooked: Detours failed (error {}), the vtable page refused VirtualProtect (error {}), and the vtable was unreadable at that slot", a_idx, a_detourError, protectResult);
			return original;
		}
		clone[a_idx] = reinterpret_cast<std::uintptr_t>(a_thunk);
		*static_cast<std::uintptr_t**>(a_object) = clone.get();
		clonedVTables[a_object] = std::move(clone);
		logger::warn("[Hooks] Detours could not patch virtual slot {} (error {}) and the vtable page refused VirtualProtect (error {}); repointed the object at a patched vtable clone", a_idx, a_detourError, protectResult);
		return original;
	}
}
