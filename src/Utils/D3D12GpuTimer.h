#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <d3d12.h>
#include <winrt/base.h>

class Profiler;

namespace Util
{
	/**
	 * @brief Times named GPU scopes in D3D12 command lists and reports them to the Profiler as external samples.
	 * @note Call on the thread that calls Profiler::EndFrame. Every call is a no-op after a failed Initialize.
	 */
	class D3D12GpuTimer
	{
	public:
		static constexpr std::uint32_t kMarksPerRecord = 8;
		static constexpr std::uint32_t kIntervalsPerRecord = 4;

		/** @brief One per submission; reused for every list recorded into it. */
		struct Record
		{
			static constexpr std::uint32_t kOpen = UINT32_MAX;

			struct Interval
			{
				std::string_view name;
				std::uint32_t begin = 0;
				std::uint32_t end = kOpen;
			};

			std::uint32_t base = 0;
			std::uint32_t marks = 0;
			std::uint32_t intervalCount = 0;
			bool recording = false;
			bool pending = false;
			std::array<Interval, kIntervalsPerRecord> intervals;

			/** @brief Forgets a committed record so Collect skips it. */
			void Discard() noexcept { pending = false; }
		};

		/**
		 * @brief Creates the timestamp query heap and readback buffer.
		 * @param a_device D3D12 device.
		 * @param a_queue Queue whose lists are timed; supplies the timestamp frequency.
		 * @param a_records Number of records that can be reserved.
		 * @param a_profiler Receives the timings; outlives this timer.
		 * @return False if timestamps are unavailable; the timer then no-ops.
		 */
		bool Initialize(ID3D12Device* a_device, ID3D12CommandQueue* a_queue, std::uint32_t a_records, Profiler& a_profiler);

		/** @brief Releases the GPU objects. Wait for in-flight lists first. */
		void Release() noexcept;

		/** @brief Hands out the next block of timestamp slots. */
		[[nodiscard]] Record Reserve() noexcept;

		/** @brief Starts a record after its list is reset; it records only while the Profiler is active. */
		void Begin(Record& a_record) const noexcept;

		/**
		 * @brief Opens a named scope.
		 * @param a_name Must outlive Collect; pass a literal. It is the Profiler pass name.
		 */
		void BeginScope(ID3D12GraphicsCommandList* a_list, Record& a_record, std::string_view a_name) const noexcept;

		/** @brief Closes the open scope of that name. */
		void EndScope(ID3D12GraphicsCommandList* a_list, Record& a_record, std::string_view a_name) const noexcept;

		/** @brief Copies the record's timestamps to the readback buffer; call before Close. */
		void Resolve(ID3D12GraphicsCommandList* a_list, const Record& a_record) const noexcept;

		/** @brief Marks the record readable once its list executes; call after ExecuteCommandLists. */
		void Commit(Record& a_record) const noexcept;

		/** @brief Reports the record's scopes to the Profiler; call once its list is known complete (fence passed). */
		void Collect(Record& a_record) const noexcept;

		/** @brief Objects in-flight lists may still reference, for deferred release. */
		[[nodiscard]] std::array<IUnknown*, 2> Resources() const noexcept { return { _heap.get(), _readback.get() }; }

	private:
		void Mark(ID3D12GraphicsCommandList* a_list, Record& a_record) const noexcept;

		winrt::com_ptr<ID3D12QueryHeap> _heap;
		winrt::com_ptr<ID3D12Resource> _readback;
		const std::uint64_t* _mapped = nullptr;
		std::uint64_t _frequency = 0;
		std::uint32_t _records = 0;
		std::uint32_t _nextRecord = 0;
		Profiler* _profiler = nullptr;
	};
}
