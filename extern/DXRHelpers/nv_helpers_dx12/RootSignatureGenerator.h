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

Utility class to create root signatures. The order in which the addition methods are called is
important as it will define the slots of the heap or of the Shader Binding Table to which buffer
pointers will be bound.

Example to create an empty root signature:
nv_helpers_dx12::RootSignatureGenerator rsc;
return rsc.Generate(m_device.Get(), true);

Example to create a signature with one constant buffer:
nv_helpers_dx12::RootSignatureGenerator rsc;
rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV);
return rsc.Generate(m_device.Get(), true);

More advance root signature:
nv_helpers_dx12::RootSignatureGenerator rsc;
rsc.AddRangeParameter({{0,1,0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0},
{0,1,0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1},
{0,1,0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2}});
return rsc.Generate(m_device.Get(), true);

*/

#pragma once

#include "d3d12.h"

#include <tuple>
#include <vector>

namespace nv_helpers_dx12
{

class RootSignatureGenerator
{
public:
  /// Add a set of heap range descriptors as a parameter of the root signature.
  void AddHeapRangesParameter(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges);

  /// Add a set of heap ranges as a parameter of the root signature. Each range
  /// is defined as follows:
  /// - UINT BaseShaderRegister: the first register index in the range, e.g. the
  /// register of a UAV with BaseShaderRegister==0 is defined in the HLSL code
  /// as register(u0)
  /// - UINT NumDescriptors: number of descriptors in the range. Those will be
  /// mapped to BaseShaderRegister, BaseShaderRegister+1 etc. UINT
  /// RegisterSpace: Allows using the same register numbers multiple times by
  /// specifying a space where they are defined, similarly to a namespace. For
  /// example, a UAV with BaseShaderRegister==0 and RegisterSpace==1 is accessed
  /// in HLSL using the syntax register(u0, space1)
  /// - D3D12_DESCRIPTOR_RANGE_TYPE RangeType: The range type, such as
  /// D3D12_DESCRIPTOR_RANGE_TYPE_CBV for a constant buffer
  /// - UINT OffsetInDescriptorsFromTableStart: The first slot in the heap
  /// corresponding to the buffers mapped by the root signature. This can either
  /// be explicit, or implicit using D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND. In
  /// this case the index in the heap is the one directly following the last
  /// parameter range (or 0 if it's the first)
  void AddHeapRangesParameter(std::vector<std::tuple<UINT, // BaseShaderRegister,
                                                     UINT, // NumDescriptors
                                                     UINT, // RegisterSpace
                                                     D3D12_DESCRIPTOR_RANGE_TYPE, // RangeType
                                                     UINT // OffsetInDescriptorsFromTableStart
                                                     >>
                                  ranges);

  /// Add a root parameter to the shader, defined by its type: constant buffer (CBV), shader
  /// resource (SRV), unordered access (UAV), or root constant (CBV, directly defined by its value
  /// instead of a buffer). The shaderRegister and registerSpace indicate how to access the
  /// parameter in the HLSL code, e.g a SRV with shaderRegister==1 and registerSpace==0 is
  /// accessible via register(t1, space0).
  /// In case of a root constant, the last parameter indicates how many successive 32-bit constants
  /// will be bound.
  void AddRootParameter(D3D12_ROOT_PARAMETER_TYPE type, UINT shaderRegister = 0,
                        UINT registerSpace = 0, UINT numRootConstants = 1);

  /// Create the root signature from the set of parameters, in the order of the addition calls
  ID3D12RootSignature* Generate(ID3D12Device* device, bool isLocal);

private:
  /// Heap range descriptors
  std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> m_ranges;
  /// Root parameter descriptors
  std::vector<D3D12_ROOT_PARAMETER> m_parameters;

  /// For each entry of m_parameter, indicate the index of the range array in m_ranges, and ~0u if
  /// the parameter is not a heap range descriptor
  std::vector<UINT> m_rangeLocations;

  enum
  {
    RSC_BASE_SHADER_REGISTER = 0,
    RSC_NUM_DESCRIPTORS = 1,
    RSC_REGISTER_SPACE = 2,
    RSC_RANGE_TYPE = 3,
    RSC_OFFSET_IN_DESCRIPTORS_FROM_TABLE_START = 4
  };
};
} // namespace nv_helpers_dx12
