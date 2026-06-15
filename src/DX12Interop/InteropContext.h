#pragma once

#include "DX12Interop.h"

#include <directx/d3dx12.h>

// Each execute call in the same frame requires its own context to avoid command allocator/list reuse hazards while frames are in flight.
class InteropContext
{
	InteropContext() 
	{
		auto interop = globals::dx12Interop;
		for (size_t i = 0; i < DX12Interop::kMaxFramesInFlight; i++) {
			DX::ThrowIfFailed(interop->d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[i].commandAllocator)));
			DX::ThrowIfFailed(interop->d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[i].commandAllocator.get(), nullptr, IID_PPV_ARGS(&frames[i].commandList)));
			frames[i].commandList->Close();
		}
	};

	// Provides buffering for D3D12 command list recording and submission.
	struct Frame
	{
		winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
		winrt::com_ptr<ID3D12GraphicsCommandList4> commandList;
		UINT64 fenceValueAtSubmission = 0;  // what value was signaled when this frame was submitted
	};

	Frame frames[DX12Interop::kMaxFramesInFlight];

public:
	static InteropContext* Make()
	{
		return new InteropContext();
	}

	// Fences the GPU, waits for D3D11 to be idle, executes D3D12 commands in the provided function, then waits for D3D12 to be idle before returning.
	template <typename Func, typename Func2 = std::nullptr_t>
	void Execute(Func func, Func2 func2 = nullptr)
	{
		auto interop = globals::dx12Interop;

		// Get to next frame context
		auto& frame = frames[interop->GetFrameContextIndex()];

		// CPU-side wait: stall if this frame's previous submission isn't done yet
		// (i.e. we've lapped the GPU)
		if (frame.fenceValueAtSubmission != 0) {
			if (interop->d3d12Fence->GetCompletedValue() < frame.fenceValueAtSubmission) {
				// GPU hasn't finished with this allocator yet - stall CPU
				DX::ThrowIfFailed(interop->d3d12Fence->SetEventOnCompletion(frame.fenceValueAtSubmission, interop->fenceEvent));
				WaitForSingleObject(interop->fenceEvent, INFINITE);
			}
		}

		// Safe to reset now - GPU is done with this allocator
		DX::ThrowIfFailed(frame.commandAllocator->Reset());
		DX::ThrowIfFailed(frame.commandList->Reset(frame.commandAllocator.get(), nullptr));

		// DX11 -> DX12 handoff
		DX::ThrowIfFailed(interop->d3d11Context->Signal(interop->d3d11Fence.get(), ++interop->currentFenceValue));
		DX::ThrowIfFailed(interop->commandQueue->Wait(interop->d3d12Fence.get(), interop->currentFenceValue));

		func(frame.commandList.get());
		DX::ThrowIfFailed(frame.commandList->Close());

		ID3D12CommandList* lists[] = { frame.commandList.get() };
		interop->commandQueue->ExecuteCommandLists(1, lists);

		if constexpr (!std::is_same_v<std::remove_cvref_t<Func2>, std::nullptr_t>)
			func2();

		// DX12 -> DX11 handoff
		DX::ThrowIfFailed(interop->commandQueue->Signal(interop->d3d12Fence.get(), ++interop->currentFenceValue));
		DX::ThrowIfFailed(interop->d3d11Context->Wait(interop->d3d11Fence.get(), interop->currentFenceValue));

		// Record what fence value this frame context was submitted at
		frame.fenceValueAtSubmission = interop->currentFenceValue;
	}
};