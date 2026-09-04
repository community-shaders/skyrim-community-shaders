#include "VTableHookFallback.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
	inline constexpr std::size_t maxClonedVTableSlots = 256;

	// Objects that now point at a vtable clone owned by this DLL. A later hook on the same
	// object must patch the same clone. The map is leaked on purpose: the game still calls
	// through the clones after this DLL's static destructors have run.
	std::mutex clonedVTableMutex;
	auto& clonedVTables = *new std::unordered_map<void*, std::unique_ptr<std::uintptr_t[]>>();

	// Copies vtable slots one page at a time and stops at the first page that faults,
	// because VirtualQuery cannot always report how far the vtable stays readable.
	// Returns the number of slots copied.
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

		// Check the bound before reading the slot: the object may already point at a clone,
		// which has exactly maxClonedVTableSlots entries.
		if (a_idx >= maxClonedVTableSlots) {
			logger::warn("[Hooks] virtual slot {} exceeds the supported clone size {}; left unhooked (Detours error {})", a_idx, maxClonedVTableSlots, a_detourError);
			return 0;
		}

		// Read the vtable pointer under the lock: another thread may have just pointed this
		// object at a clone, and the clone check below must see that.
		auto vtable = *static_cast<std::uintptr_t**>(a_object);
		const auto original = vtable[a_idx];

		// If an earlier hook already pointed this object at a clone, patch that clone; a new
		// clone would drop the earlier hook. `original` was read from the clone, so the hooks
		// still chain.
		if (auto it = clonedVTables.find(a_object); it != clonedVTables.end() && it->second.get() == vtable) {
			it->second[a_idx] = reinterpret_cast<std::uintptr_t>(a_thunk);
			logger::warn("[Hooks] Detours could not patch virtual slot {} (error {}); patched the object's existing vtable clone", a_idx, a_detourError);
			return original;
		}

		MEMORY_BASIC_INFORMATION mbi{};
		const bool querySucceeded = VirtualQuery(vtable, &mbi, sizeof(mbi)) == sizeof(mbi);

		// Swap the slot in place, keeping execute rights if the page has them.
		constexpr DWORD executeProtects = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		const DWORD writableProtect = (querySucceeded && (mbi.Protect & executeProtects) != 0) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
		DWORD previousProtect = 0;
		if (VirtualProtect(&vtable[a_idx], sizeof(std::uintptr_t), writableProtect, &previousProtect)) {
			vtable[a_idx] = reinterpret_cast<std::uintptr_t>(a_thunk);
			DWORD restoredProtect = 0;
			if (!VirtualProtect(&vtable[a_idx], sizeof(std::uintptr_t), previousProtect, &restoredProtect))
				logger::warn("[Hooks] could not restore protection {:#x} on virtual slot {} (error {})", previousProtect, a_idx, GetLastError());
			logger::warn("[Hooks] Detours could not patch virtual slot {} (error {}); swapped the vtable slot in place", a_idx, a_detourError);
			return original;
		}
		const DWORD protectError = GetLastError();

		// Last option: clone the vtable, patch the clone, and point the object at it. This
		// needs no protection change. Copy every readable slot, not only a_idx, so the other
		// virtual functions keep working through the clone.
		auto clone = std::make_unique<std::uintptr_t[]>(maxClonedVTableSlots);
		if (CopyReadableSlots(clone.get(), vtable, maxClonedVTableSlots) <= a_idx) {
			logger::warn("[Hooks] virtual slot {} could not be hooked: Detours failed (error {}), the vtable page refused VirtualProtect (error {}), and the vtable was unreadable at that slot", a_idx, a_detourError, protectError);
			return original;
		}
		clone[a_idx] = reinterpret_cast<std::uintptr_t>(a_thunk);
		// Store the clone before pointing the object at it, so a throwing insert cannot
		// leave the object with a dangling vtable pointer.
		auto& storedClone = clonedVTables[a_object];
		storedClone = std::move(clone);
		*static_cast<std::uintptr_t**>(a_object) = storedClone.get();
		logger::warn("[Hooks] Detours could not patch virtual slot {} (error {}) and the vtable page refused VirtualProtect (error {}); repointed the object at a patched vtable clone", a_idx, a_detourError, protectError);
		return original;
	}
}
