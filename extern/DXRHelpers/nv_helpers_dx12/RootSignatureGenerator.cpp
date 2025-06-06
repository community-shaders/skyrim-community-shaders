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

*/

#include "RootSignatureGenerator.h"

namespace nv_helpers_dx12
{

//--------------------------------------------------------------------------------------------------
//
// Add a set of heap range descriptors as a parameter of the root signature.
void RootSignatureGenerator::AddHeapRangesParameter(
    const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges)
{
  m_ranges.push_back(ranges);

  // A set of ranges on the heap is a descriptor table parameter
  D3D12_ROOT_PARAMETER param = {};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.size());
  // The range pointer is kept null here, and will be resolved when generating the root signature
  // (see explanation of m_rangeLocations below)
  param.DescriptorTable.pDescriptorRanges = nullptr;

  // All parameters (heap ranges and root parameters) are added to the same parameter list to
  // preserve order
  m_parameters.push_back(param);

  // The descriptor table descriptor ranges require a pointer to the descriptor ranges. Since new
  // ranges can be dynamically added in the vector, we separately store the index of the range set.
  // The actual address will be solved when generating the actual root signature
  m_rangeLocations.push_back(static_cast<UINT>(m_ranges.size() - 1));
}

//--------------------------------------------------------------------------------------------------
//
// Add a set of heap ranges as a parameter of the root signature. Each range
// is defined as follows:
// - UINT BaseShaderRegister: the first register index in the range, e.g. the
// register of a UAV with BaseShaderRegister==0 is defined in the HLSL code
// as register(u0)
// - UINT NumDescriptors: number of descriptors in the range. Those will be
// mapped to BaseShaderRegister, BaseShaderRegister+1 etc. UINT
// RegisterSpace: Allows using the same register numbers multiple times by
// specifying a space where they are defined, similarly to a namespace. For
// example, a UAV with BaseShaderRegister==0 and RegisterSpace==1 is accessed
// in HLSL using the syntax register(u0, space1)
// - D3D12_DESCRIPTOR_RANGE_TYPE RangeType: The range type, such as
// D3D12_DESCRIPTOR_RANGE_TYPE_CBV for a constant buffer
// - UINT OffsetInDescriptorsFromTableStart: The first slot in the heap
// corresponding to the buffers mapped by the root signature. This can either
// be explicit, or implicit using D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND. In
// this case the index in the heap is the one directly following the last
// parameter range (or 0 if it's the first)
void RootSignatureGenerator::AddHeapRangesParameter(
    std::vector<std::tuple<UINT, /* BaseShaderRegister, */ UINT, /* NumDescriptors */ UINT,
                           /* RegisterSpace */ D3D12_DESCRIPTOR_RANGE_TYPE,
                           /* RangeType */ UINT /* OffsetInDescriptorsFromTableStart */>>
        ranges)
{
  // Build and store the set of descriptors for the ranges
  std::vector<D3D12_DESCRIPTOR_RANGE> rangeStorage;
  for (const auto& input : ranges)
  {
    D3D12_DESCRIPTOR_RANGE r = {};
    r.BaseShaderRegister = std::get<RSC_BASE_SHADER_REGISTER>(input);
    r.NumDescriptors = std::get<RSC_NUM_DESCRIPTORS>(input);
    r.RegisterSpace = std::get<RSC_REGISTER_SPACE>(input);
    r.RangeType = std::get<RSC_RANGE_TYPE>(input);
    r.OffsetInDescriptorsFromTableStart =
        std::get<RSC_OFFSET_IN_DESCRIPTORS_FROM_TABLE_START>(input);
    rangeStorage.push_back(r);
  }

  // Add those ranges to the heap parameters
  AddHeapRangesParameter(rangeStorage);
}

//--------------------------------------------------------------------------------------------------
//
// Add a root parameter to the shader, defined by its type: constant buffer (CBV), shader
// resource (SRV), unordered access (UAV), or root constant (CBV, directly defined by its value
// instead of a buffer). The shaderRegister and registerSpace indicate how to access the
// parameter in the HLSL code, e.g a SRV with shaderRegister==1 and registerSpace==0 is
// accessible via register(t1, space0).
// In case of a root constant, the last parameter indicates how many successive 32-bit constants
// will be bound.
void RootSignatureGenerator::AddRootParameter(D3D12_ROOT_PARAMETER_TYPE type,
                                              UINT shaderRegister /*= 0*/,
                                              UINT registerSpace /*= 0*/,
                                              UINT numRootConstants /*= 1*/)
{
  D3D12_ROOT_PARAMETER param = {};
  param.ParameterType = type;
  // The descriptor is an union, so specific values need to be set in case the parameter is a
  // constant instead of a buffer.
  if (type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
  {
    param.Constants.Num32BitValues = numRootConstants;
    param.Constants.RegisterSpace = registerSpace;
    param.Constants.ShaderRegister = shaderRegister;
  }
  else
  {
    param.Descriptor.RegisterSpace = registerSpace;
    param.Descriptor.ShaderRegister = shaderRegister;
  }

  // We default the visibility to all shaders
  param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Add the root parameter to the set of parameters,
  m_parameters.push_back(param);
  // and indicate that there will be no range
  // location to indicate since this parameter is not part of the heap
  m_rangeLocations.push_back(~0u);
}

//--------------------------------------------------------------------------------------------------
//
// Create the root signature from the set of parameters, in the order of the addition calls
ID3D12RootSignature* RootSignatureGenerator::Generate(ID3D12Device* device, bool isLocal)
{
  // Go through all the parameters, and set the actual addresses of the heap range descriptors based
  // on their indices in the range set array
  for (size_t i = 0; i < m_parameters.size(); i++)
  {
    if (m_parameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
    {
      m_parameters[i].DescriptorTable.pDescriptorRanges = m_ranges[m_rangeLocations[i]].data();
    }
  }
  // Specify the root signature with its set of parameters
  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = static_cast<UINT>(m_parameters.size());
  rootDesc.pParameters = m_parameters.data();
  // Set the flags of the signature. By default root signatures are global, for example for vertex
  // and pixel shaders. For raytracing shaders the root signatures are local.
  rootDesc.Flags =
      isLocal ? D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE : D3D12_ROOT_SIGNATURE_FLAG_NONE;

  // Create the root signature from its descriptor
  ID3DBlob* pSigBlob;
  ID3DBlob* pErrorBlob;
  HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &pSigBlob,
                                           &pErrorBlob);
  if (FAILED(hr))
  {
    throw std::logic_error("Cannot serialize root signature");
  }
  ID3D12RootSignature* pRootSig;
  hr = device->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&pRootSig));
  if (FAILED(hr))
  {
    throw std::logic_error("Cannot create root signature");
  }
  return pRootSig;
}

} // namespace nv_helpers_dx12
