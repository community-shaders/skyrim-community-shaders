/******************************************************************************
 * Copyright 1998-2018 NVIDIA Corp. All Rights Reserved.
 *****************************************************************************/

#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <d3d12.h>
#include "DXSampleHelper.h"
#include <dxcapi.h>

#include <vector>

namespace nv_helpers_dx12
{

//--------------------------------------------------------------------------------------------------
//
//
inline ID3D12Resource* CreateBuffer(ID3D12Device* m_device, uint64_t size,
                                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState,
                                    const D3D12_HEAP_PROPERTIES& heapProps)
{
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Alignment = 0;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Flags = flags;
  bufDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufDesc.Height = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufDesc.MipLevels = 1;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.SampleDesc.Quality = 0;
  bufDesc.Width = size;

  ID3D12Resource* pBuffer;
  ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                  initState, nullptr, IID_PPV_ARGS(&pBuffer)));
  return pBuffer;
}

#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif

// Specifies a heap used for uploading. This heap type has CPU access optimized
// for uploading to the GPU.
static const D3D12_HEAP_PROPERTIES kUploadHeapProps = {
    D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};

// Specifies the default heap. This heap type experiences the most bandwidth for
// the GPU, but cannot provide CPU access.
static const D3D12_HEAP_PROPERTIES kDefaultHeapProps = {
    D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};

//--------------------------------------------------------------------------------------------------
// Compile a HLSL file into a DXIL library
//
IDxcBlob* CompileShaderLibrary(LPCWSTR fileName)
{
  static IDxcCompiler* pCompiler = nullptr;
  static IDxcLibrary* pLibrary = nullptr;
  static IDxcIncludeHandler* dxcIncludeHandler;

  HRESULT hr;

  // Initialize the DXC compiler and compiler helper
  if (!pCompiler)
  {
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void **)&pCompiler));
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&pLibrary));
    ThrowIfFailed(pLibrary->CreateIncludeHandler(&dxcIncludeHandler));
  }
  // Open and read the file
  std::ifstream shaderFile(fileName);
  if (shaderFile.good() == false)
  {
    throw std::logic_error("Cannot find shader file");
  }
  std::stringstream strStream;
  strStream << shaderFile.rdbuf();
  std::string sShader = strStream.str();

  // Create blob from the string
  IDxcBlobEncoding* pTextBlob;
  ThrowIfFailed(pLibrary->CreateBlobWithEncodingFromPinned(
      (LPBYTE)sShader.c_str(), (uint32_t)sShader.size(), 0, &pTextBlob));

  // Compile
  IDxcOperationResult* pResult;
  ThrowIfFailed(pCompiler->Compile(pTextBlob, fileName, L"", L"lib_6_3", nullptr, 0, nullptr, 0,
                                   dxcIncludeHandler, &pResult));

  // Verify the result
  HRESULT resultCode;
  ThrowIfFailed(pResult->GetStatus(&resultCode));
  if (FAILED(resultCode))
  {
    IDxcBlobEncoding* pError;
    hr = pResult->GetErrorBuffer(&pError);
    if (FAILED(hr))
    {
      throw std::logic_error("Failed to get shader compiler error");
    }

    // Convert error blob to a string
    std::vector<char> infoLog(pError->GetBufferSize() + 1);
    memcpy(infoLog.data(), pError->GetBufferPointer(), pError->GetBufferSize());
    infoLog[pError->GetBufferSize()] = 0;

    std::string errorMsg = "Shader Compiler Error:\n";
    errorMsg.append(infoLog.data());

    MessageBoxA(nullptr, errorMsg.c_str(), "Error!", MB_OK);
    throw std::logic_error("Failed compile shader");
  }

  IDxcBlob* pBlob;
  ThrowIfFailed(pResult->GetResult(&pBlob));
  return pBlob;
}

//--------------------------------------------------------------------------------------------------
//
//
ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, uint32_t count,
                                           D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.NumDescriptors = count;
  desc.Type = type;
  desc.Flags =
      shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

  ID3D12DescriptorHeap* pHeap;
  ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)));
  return pHeap;
}

//--------------------------------------------------------------------------------------------------
//
//
template <class Vertex>
void GenerateMengerSponge(int32_t level, float probability, std::vector<Vertex>& outputVertices,
                          std::vector<UINT>& outputIndices)
{
  struct Cube
  {
    Cube(const XMVECTOR& tlf, float s) : m_topLeftFront(tlf), m_size(s)
    {
    }
    XMVECTOR m_topLeftFront;
    float m_size;

    void enqueueQuad(std::vector<Vertex>& vertices, std::vector<UINT>& indices,
                     const XMVECTOR& bottomLeft4, const XMVECTOR& dx, const XMVECTOR& dy, bool flip)
    {
      UINT currentIndex = static_cast<UINT>(vertices.size());
      XMFLOAT3 bottomLeft(bottomLeft4.m128_f32);
      XMVECTOR normal = XMVector3Cross(XMVector3Normalize(dy), XMVector3Normalize(dx));
      if (flip)
      {
        normal = -normal;

        indices.push_back(currentIndex + 0);
        indices.push_back(currentIndex + 2);
        indices.push_back(currentIndex + 1);

        indices.push_back(currentIndex + 3);
        indices.push_back(currentIndex + 1);
        indices.push_back(currentIndex + 2);
      }
      else
      {

        indices.push_back(currentIndex + 0);
        indices.push_back(currentIndex + 1);
        indices.push_back(currentIndex + 2);

        indices.push_back(currentIndex + 2);
        indices.push_back(currentIndex + 1);
        indices.push_back(currentIndex + 3);
      }

      const DirectX::XMFLOAT4 n = {normal.m128_f32[0], normal.m128_f32[1], normal.m128_f32[2], 0.f};
      vertices.push_back(
          {{bottomLeft.x, bottomLeft.y, bottomLeft.z, 1.f}, n, {1.f, 0.f, 0.f, 1.f}});
      vertices.push_back({{bottomLeft.x + dx.m128_f32[0], bottomLeft.y + dx.m128_f32[1],
                           bottomLeft.z + dx.m128_f32[2], 1.f},
                          n,
                          {0.5f, 1.f, 0.f, 1.f}});
      vertices.push_back({{bottomLeft.x + dy.m128_f32[0], bottomLeft.y + dy.m128_f32[1],
                           bottomLeft.z + dy.m128_f32[2], 1.f},
                          n,
                          {0.5f, 0.f, 1.f, 1.f}});

      vertices.push_back({{bottomLeft.x + dx.m128_f32[0] + dy.m128_f32[0],
                           bottomLeft.y + dx.m128_f32[1] + dy.m128_f32[1],
                           bottomLeft.z + dx.m128_f32[2] + dy.m128_f32[2], 1.f},
                          n,
                          {0.f, 1.f, 0.f, 1.f}});
    }
    void enqueueVertices(std::vector<Vertex>& vertices, std::vector<UINT>& indices)
    {

      XMVECTOR current = m_topLeftFront;
      enqueueQuad(vertices, indices, current, {m_size, 0, 0}, {0, m_size, 0}, false);
      enqueueQuad(vertices, indices, current, {m_size, 0, 0}, {0, 0, m_size}, true);
      enqueueQuad(vertices, indices, current, {0, m_size, 0}, {0, 0, m_size}, false);

      current.m128_f32[0] += m_size;
      current.m128_f32[1] += m_size;
      current.m128_f32[2] += m_size;
      enqueueQuad(vertices, indices, current, {-m_size, 0, 0}, {0, -m_size, 0}, true);
      enqueueQuad(vertices, indices, current, {-m_size, 0, 0}, {0, 0, -m_size}, false);
      enqueueQuad(vertices, indices, current, {0, -m_size, 0}, {0, 0, -m_size}, true);
    }
    void split(std::vector<Cube>& cubes)
    {
      float size = m_size / 3.f;
      XMVECTOR topLeftFront = m_topLeftFront;
      for (int x = 0; x < 3; x++)
      {
        topLeftFront.m128_f32[0] = m_topLeftFront.m128_f32[0] + static_cast<float>(x) * size;
        for (int y = 0; y < 3; y++)
        {
          if (x == 1 && y == 1)
            continue;
          topLeftFront.m128_f32[1] = m_topLeftFront.m128_f32[1] + static_cast<float>(y) * size;
          for (int z = 0; z < 3; z++)
          {
            if (x == 1 && z == 1)
              continue;
            if (y == 1 && z == 1)
              continue;

            topLeftFront.m128_f32[2] = m_topLeftFront.m128_f32[2] + static_cast<float>(z) * size;
            cubes.push_back({topLeftFront, size});
          }
        }
      }
    }

    void splitProb(std::vector<Cube>& cubes, float prob)
    {

      float size = m_size / 3.f;
      XMVECTOR topLeftFront = m_topLeftFront;
      for (int x = 0; x < 3; x++)
      {
        topLeftFront.m128_f32[0] = m_topLeftFront.m128_f32[0] + static_cast<float>(x) * size;
        for (int y = 0; y < 3; y++)
        {
          topLeftFront.m128_f32[1] = m_topLeftFront.m128_f32[1] + static_cast<float>(y) * size;
          for (int z = 0; z < 3; z++)
          {
            float sample = rand() / static_cast<float>(RAND_MAX);
            if (sample > prob)
              continue;
            topLeftFront.m128_f32[2] = m_topLeftFront.m128_f32[2] + static_cast<float>(z) * size;
            cubes.push_back({topLeftFront, size});
          }
        }
      }
    }
  };

  XMVECTOR orig;
  orig.m128_f32[0] = -0.5f;
  orig.m128_f32[1] = -0.5f;
  orig.m128_f32[2] = -0.5f;
  orig.m128_f32[3] = 1.f;

  Cube cube(orig, 1.f);

  std::vector<Cube> cubes1 = {cube};
  std::vector<Cube> cubes2 = {};

  auto previous = &cubes1;
  auto next = &cubes2;

  for (int i = 0; i < level; i++)
  {
    for (Cube& c : *previous)
    {
      if (probability < 0.f)
        c.split(*next);
      else
        c.splitProb(*next, 20.f / 27.f);
    }
    auto temp = previous;
    previous = next;
    next = temp;
    next->clear();
  }

  outputVertices.reserve(24 * previous->size());
  outputIndices.reserve(24 * previous->size());
  for (Cube& c : *previous)
  {
    c.enqueueVertices(outputVertices, outputIndices);
  }
}

} // namespace nv_helpers_dx12
