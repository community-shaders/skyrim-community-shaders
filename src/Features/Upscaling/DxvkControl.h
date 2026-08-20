#pragma once

#include <atomic>
#include <cstdint>

class DxvkControl
{
public:
	struct Operations
	{
		bool (*setSynchronousPresent)(bool) = nullptr;
		bool (*setPresentQueueDepth)(uint32_t) = nullptr;
		bool (*requestSwapchainRecreate)() = nullptr;
	};

	enum class PresentQueuePolicy : uint32_t
	{
		kSynchronous = 0,
		kBoundedOverlap = 2,
		kUnrestricted = UINT32_MAX,
	};

	DxvkControl();
	explicit DxvkControl(Operations a_operations) : operations(a_operations) {}

	void RequestSwapchainRecreate(const char* a_reason = "FG method switch") const;
	void SetPresentQueuePolicy(PresentQueuePolicy a_policy);

private:
	void SetSynchronousPresent(bool a_sync);
	std::atomic<int> appliedSync{ -1 };
	std::atomic<uint32_t> appliedQueueDepth{ UINT32_MAX - 1u };
	std::atomic<bool> warnedSyncUnavailable{ false };
	Operations operations;
};
