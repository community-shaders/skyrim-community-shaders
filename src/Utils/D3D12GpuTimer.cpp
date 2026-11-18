#include "Utils/D3D12GpuTimer.h"

#include "Profiler.h"

#include <utility>

namespace Util
{
	bool D3D12GpuTimer::Initialize(
		ID3D12Device* a_device, ID3D12CommandQueue* a_queue, std::uint32_t a_records, Profiler& a_profiler)
	{
		Release();
		const std::uint32_t slots = a_records * kMarksPerRecord;
		if (!a_device || !a_queue || !slots)
			return false;

		const D3D12_QUERY_HEAP_DESC heapDesc{
			.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
			.Count = slots
		};
		const D3D12_HEAP_PROPERTIES heapProperties{ .Type = D3D12_HEAP_TYPE_READBACK };
		const D3D12_RESOURCE_DESC bufferDesc{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Width = slots * sizeof(std::uint64_t),
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.SampleDesc = { .Count = 1 },
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR
		};
		void* mapped = nullptr;
		std::uint64_t frequency = 0;
		if (FAILED(a_device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(_heap.put()))) ||
			FAILED(a_device->CreateCommittedResource(
				&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_readback.put()))) ||
			FAILED(a_queue->GetTimestampFrequency(&frequency)) || !frequency ||
			FAILED(_readback->Map(0, nullptr, &mapped)) || !mapped) {
			Release();
			return false;
		}
		_heap->SetName(L"D3D12GpuTimer::QueryHeap");
		_readback->SetName(L"D3D12GpuTimer::Readback");
		_mapped = static_cast<const std::uint64_t*>(mapped);
		_frequency = frequency;
		_records = a_records;
		_profiler = &a_profiler;
		return true;
	}

	void D3D12GpuTimer::Release() noexcept
	{
		if (_readback && _mapped)
			_readback->Unmap(0, nullptr);
		_mapped = nullptr;
		_readback = nullptr;
		_heap = nullptr;
		_frequency = 0;
		_records = 0;
		_nextRecord = 0;
		_profiler = nullptr;
	}

	D3D12GpuTimer::Record D3D12GpuTimer::Reserve() noexcept
	{
		Record record;
		if (_nextRecord < _records)
			record.base = _nextRecord++ * kMarksPerRecord;
		return record;
	}

	void D3D12GpuTimer::Begin(Record& a_record) const noexcept
	{
		a_record.marks = 0;
		a_record.intervalCount = 0;
		a_record.pending = false;
		a_record.recording = _heap && _profiler->IsActive();
	}

	void D3D12GpuTimer::Mark(ID3D12GraphicsCommandList* a_list, Record& a_record) const noexcept
	{
		a_list->EndQuery(_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, a_record.base + a_record.marks);
		++a_record.marks;
	}

	void D3D12GpuTimer::BeginScope(
		ID3D12GraphicsCommandList* a_list, Record& a_record, std::string_view a_name) const noexcept
	{
		if (!a_record.recording || a_record.marks >= kMarksPerRecord ||
			a_record.intervalCount >= kIntervalsPerRecord)
			return;
		a_record.intervals[a_record.intervalCount++] = { .name = a_name, .begin = a_record.marks };
		Mark(a_list, a_record);
	}

	void D3D12GpuTimer::EndScope(
		ID3D12GraphicsCommandList* a_list, Record& a_record, std::string_view a_name) const noexcept
	{
		if (!a_record.recording || a_record.marks >= kMarksPerRecord)
			return;
		for (std::uint32_t i = 0; i < a_record.intervalCount; ++i) {
			auto& interval = a_record.intervals[i];
			if (interval.end == Record::kOpen && interval.name == a_name) {
				interval.end = a_record.marks;
				Mark(a_list, a_record);
				return;
			}
		}
	}

	void D3D12GpuTimer::Resolve(ID3D12GraphicsCommandList* a_list, const Record& a_record) const noexcept
	{
		if (!a_record.recording || !a_record.marks)
			return;
		a_list->ResolveQueryData(
			_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, a_record.base, a_record.marks,
			_readback.get(), a_record.base * sizeof(std::uint64_t));
	}

	void D3D12GpuTimer::Commit(Record& a_record) const noexcept
	{
		a_record.pending = a_record.recording && a_record.marks != 0;
	}

	void D3D12GpuTimer::Collect(Record& a_record) const noexcept
	{
		if (!std::exchange(a_record.pending, false) || !_mapped)
			return;
		const auto* ticks = _mapped + a_record.base;
		for (std::uint32_t i = 0; i < a_record.intervalCount; ++i) {
			const auto& interval = a_record.intervals[i];
			if (interval.end == Record::kOpen || ticks[interval.end] < ticks[interval.begin])
				continue;
			const double ms = static_cast<double>(ticks[interval.end] - ticks[interval.begin]) * 1000.0 /
			                  static_cast<double>(_frequency);
			_profiler->RecordExternalSample(interval.name, static_cast<float>(ms));
		}
		a_record.intervalCount = 0;
	}
}
