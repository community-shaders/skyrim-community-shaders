/*-----------------------------------------------------------------------
Copyright (c) 2014-2018, NVIDIA. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Neither the name of its contributors may be used to endorse
or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

/*
Contacts for feedback:
- pgautron@nvidia.com (Pascal Gautron)
- mlefrancois@nvidia.com (Martin-Karl Lefrancois)

The ShaderBindingTable is a helper to construct the SBT. It helps to maintain the
proper offsets of each element, required when constructing the SBT, but also when filling the
dispatch rays description.

Example:


D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
UINT64* heapPointer = reinterpret_cast< UINT64* >(srvUavHeapHandle.ptr);

m_sbtHelper.AddRayGenerationProgram(L"RayGen", {heapPointer});
m_sbtHelper.AddMissProgram(L"Miss", {});

m_sbtHelper.AddHitGroup(L"HitGroup",
{(void*)(m_constantBuffers[i]->GetGPUVirtualAddress())});
m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});


// Create the SBT on the upload heap
uint32_t sbtSize = 0;
m_sbtHelper.ComputeSBTSize(GetRTDevice(), &sbtSize);
m_sbtStorage = nv_helpers_dx12::CreateBuffer(m_device.Get(), sbtSize,
D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ,
nv_helpers_dx12::kUploadHeapProps);

m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());


//--------------------------------------------------------------------
Then setting the descriptor for the dispatch rays become way easier
//--------------------------------------------------------------------

D3D12_DISPATCH_RAYS_DESC desc = {};
// The layout of the SBT is as follows: ray generation shaders, miss shaders,
hit groups. As
// described in the CreateShaderBindingTable method, all SBT entries have the
same size to allow
// a fixed stride.

// The ray generation shaders are always at the beginning of the SBT. In this
// example we have only one RG, so the size of this SBT sections is
m_sbtEntrySize. uint32_t rayGenerationSectionSizeInBytes =
m_sbtHelper.GetRayGenSectionSize(); desc.RayGenerationShaderRecord.StartAddress
= m_sbtStorage->GetGPUVirtualAddress();
desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

// The miss shaders are in the second SBT section, right after the ray
generation shader. We
// have one miss shader for the camera rays and one for the shadow rays, so this
section has a
// size of 2*m_sbtEntrySize. We also indicate the stride between the two miss
shaders, which is
// the size of a SBT entry
uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
desc.MissShaderTable.StartAddress =
m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

// The hit groups section start after the miss shaders. In this sample we have 4
hit groups: 2
// for the triangles (1 used when hitting the geometry from a camera ray, 1 when
hitting the
// same geometry from a shadow ray) and 2 for the plane. We also indicate the
stride between the
// two miss shaders, which is the size of a SBT entry
// #Pascal: experiment with different sizes for the SBT entries
uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
desc.HitGroupTable.StartAddress =
m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes +
missSectionSizeInBytes; desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();



*/

#pragma once

#include "d3d12.h"

#include <vector>

namespace nv_helpers_dx12
{
/// Helper class to create and maintain a Shader Binding Table
class ShaderBindingTableGenerator
{
public:
  /// Add a ray generation program by name, with its list of data pointers or values according to
  /// the layout of its root signature
  void AddRayGenerationProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData);

  /// Add a miss program by name, with its list of data pointers or values according to
  /// the layout of its root signature
  void AddMissProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData);

  /// Add a hit group by name, with its list of data pointers or values according to
  /// the layout of its root signature
  void AddHitGroup(const std::wstring& entryPoint, const std::vector<void*>& inputData);

  /// Compute the size of the SBT based on the set of programs and hit groups it contains
  uint32_t ComputeSBTSize();

  /// Build the SBT and store it into sbtBuffer, which has to be pre-allocated on the upload heap.
  /// Access to the raytracing pipeline object is required to fetch program identifiers using their
  /// names
  void Generate(ID3D12Resource* sbtBuffer,
                ID3D12StateObjectProperties* raytracingPipeline);

  /// Reset the sets of programs and hit groups
  void Reset();

  /// The following getters are used to simplify the call to DispatchRays where the offsets of the
  /// shader programs must be exactly following the SBT layout

  /// Get the size in bytes of the SBT section dedicated to ray generation programs
  UINT GetRayGenSectionSize() const;
  /// Get the size in bytes of one ray generation program entry in the SBT
  UINT GetRayGenEntrySize() const;

  /// Get the size in bytes of the SBT section dedicated to miss programs
  UINT GetMissSectionSize() const;
  /// Get the size in bytes of one miss program entry in the SBT
  UINT GetMissEntrySize();

  /// Get the size in bytes of the SBT section dedicated to hit groups
  UINT GetHitGroupSectionSize() const;
  /// Get the size in bytes of hit group entry in the SBT
  UINT GetHitGroupEntrySize() const;

private:
  /// Wrapper for SBT entries, each consisting of the name of the program and a list of values,
  /// which can be either pointers or raw 32-bit constants
  struct SBTEntry
  {
    SBTEntry(std::wstring entryPoint, std::vector<void*> inputData);

    const std::wstring m_entryPoint;
    const std::vector<void*> m_inputData;
  };

  /// For each entry, copy the shader identifier followed by its resource pointers and/or root
  /// constants in outputData, with a stride in bytes of entrySize, and returns the size in bytes
  /// actually written to outputData.
  uint32_t CopyShaderData(ID3D12StateObjectProperties* raytracingPipeline,
                          uint8_t* outputData, const std::vector<SBTEntry>& shaders,
                          uint32_t entrySize);

  /// Compute the size of the SBT entries for a set of entries, which is determined by the maximum
  /// number of parameters of their root signature
  uint32_t GetEntrySize(const std::vector<SBTEntry>& entries);

  std::vector<SBTEntry> m_rayGen;
  std::vector<SBTEntry> m_miss;
  std::vector<SBTEntry> m_hitGroup;

  /// For each category, the size of an entry in the SBT depends on the maximum number of resources
  /// used by the shaders in that category.The helper computes those values automatically in
  /// GetEntrySize()
  uint32_t m_rayGenEntrySize;
  uint32_t m_missEntrySize;
  uint32_t m_hitGroupEntrySize;

  /// The program names are translated into program identifiers.The size in bytes of an identifier
  /// is provided by the device and is the same for all categories.
  UINT m_progIdSize;
};
} // namespace nv_helpers_dx12
