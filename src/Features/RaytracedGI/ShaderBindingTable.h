#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <vector>
#include <winrt/base.h>

namespace DX12
{
	// Utility for alignment
	constexpr UINT64 Align(UINT64 size, UINT64 alignment)
	{
		return (size + alignment - 1) & ~(alignment - 1);
	}

	//
	// 1. ShaderRecord
	// -------------------------------
	class ShaderRecord
	{
	public:
		ShaderRecord() = default;

		ShaderRecord(void* shaderID, UINT shaderIDSize, const void* localArgs = nullptr, UINT localArgsSize = 0)
		{
			assert(shaderID);

			m_data.resize(shaderIDSize + localArgsSize);
			memcpy(m_data.data(), shaderID, shaderIDSize);

			if (localArgs && localArgsSize > 0)
				memcpy(m_data.data() + shaderIDSize, localArgs, localArgsSize);
		}

		UINT Size() const { return static_cast<UINT>(m_data.size()); }
		const void* Data() const { return m_data.data(); }

	private:
		std::vector<uint8_t> m_data;
	};

	//
	// 2. ShaderTableSection
	// -------------------------------
	class ShaderTableSection
	{
	public:
		void AddRecord(const ShaderRecord& record)
		{
			m_records.push_back(record);
		}

		UINT RecordCount() const { return static_cast<UINT>(m_records.size()); }

		UINT64 RecordSize() const
		{
			if (m_records.empty())
				return 0;

			UINT64 size = m_records[0].Size();
			return Align(size, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		}

		UINT64 SectionSize() const
		{
			return Align(RecordCount() * RecordSize(), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		}

		size_t NumRecords() const { return m_records.size(); }

		void CopyTo(uint8_t* dest) const
		{
			if (m_records.empty())
				return;

			UINT64 stride = RecordSize();

			for (const auto& r : m_records) {
				memcpy(dest, r.Data(), r.Size());
				if (r.Size() < stride) {
					memset(dest + r.Size(), 0, stride - r.Size());
				}
				dest += stride;
			}
		}

		bool Empty() const { return m_records.empty(); }

	private:
		std::vector<ShaderRecord> m_records;
	};

	//
	// 3. ShaderBindingTable
	// -------------------------------
	class ShaderBindingTable
	{
	public:
		ShaderTableSection RayGen;
		ShaderTableSection Miss;
		ShaderTableSection HitGroup;
		ShaderTableSection Callable;

		UINT64 GetTotalSize() const
		{
			UINT64 size = 0;

			UINT64 rayGenSize = RayGen.SectionSize();
			size = Align(size + rayGenSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

			UINT64 missSize = Miss.SectionSize();
			size += Align(size + missSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

			UINT64 hitSize = HitGroup.SectionSize();
			size += Align(size + hitSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

			UINT64 callSize = Callable.SectionSize();
			size += Align(size + callSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

			return size;
		}

		void Build(void* pData)
		{
			assert(pData && "ShaderBindingTable::Build - pData cannot be nullptr");

			uint8_t* pDataUint8 = static_cast<uint8_t*>(pData);

			auto copyAndAdvance = [&](const ShaderTableSection& section, UINT64& offset) {
				if (!section.Empty()) {
					section.CopyTo(pDataUint8 + offset);
					offset = Align(offset + section.SectionSize(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
				}
			};

			// Start at offset 0
			UINT64 offset = 0;

			m_rayGenOffset = offset;
			copyAndAdvance(RayGen, offset);

			m_missOffset = offset;
			copyAndAdvance(Miss, offset);

			m_hitOffset = offset;
			copyAndAdvance(HitGroup, offset);

			m_callOffset = offset;
			copyAndAdvance(Callable, offset);

			m_sbtSize = offset;
		}

		void LogShaderBindingTable(D3D12_GPU_VIRTUAL_ADDRESS baseAddr)
		{
			logger::info(
				"[RT] SBT Layout:\n"
				"  Base GPU VA:                0x{:016X}\n"
				"  RayGen:\n"
				"    Offset:                   {}\n"
				"    SectionSize:              {}\n"
				"    RecordSize:               {}\n"
				"    NumRecords:               {}\n"
				"    GPU VA:                   0x{:016X}\n"
				"  Miss:\n"
				"    Offset:                   {}\n"
				"    SectionSize:              {}\n"
				"    RecordSize:               {}\n"
				"    NumRecords:               {}\n"
				"    GPU VA:                   0x{:016X}\n"
				"  HitGroup:\n"
				"    Offset:                   {}\n"
				"    SectionSize:              {}\n"
				"    RecordSize:               {}\n"
				"    NumRecords:               {}\n"
				"    GPU VA:                   0x{:016X}\n"
				"  Callable:\n"
				"    Offset:                   {}\n"
				"    SectionSize:              {}\n"
				"    RecordSize:               {}\n"
				"    NumRecords:               {}\n"
				"    GPU VA:                   0x{:016X}\n",
				baseAddr,

				// RayGen
				m_rayGenOffset,
				RayGen.SectionSize(),
				RayGen.RecordSize(),
				RayGen.NumRecords(),
				baseAddr + m_rayGenOffset,

				// Miss
				m_missOffset,
				Miss.SectionSize(),
				Miss.RecordSize(),
				Miss.NumRecords(),
				baseAddr + m_missOffset,

				// HitGroup
				m_hitOffset,
				HitGroup.SectionSize(),
				HitGroup.RecordSize(),
				HitGroup.NumRecords(),
				baseAddr + m_hitOffset,

				// Callable
				m_callOffset,
				Callable.SectionSize(),
				Callable.RecordSize(),
				Callable.NumRecords(),
				baseAddr + m_callOffset);
		}

		void FillDispatchShaderBindingTable(D3D12_DISPATCH_RAYS_DESC& desc, D3D12_GPU_VIRTUAL_ADDRESS baseAddr)
		{
			desc.RayGenerationShaderRecord = {
				baseAddr + m_rayGenOffset,
				RayGen.SectionSize()
			};

			if (Miss.Empty()) {
				desc.MissShaderTable = {};
			} else {
				desc.MissShaderTable = {
					baseAddr + m_missOffset,
					Miss.SectionSize(),
					Miss.RecordSize()
				};
			}

			if (HitGroup.Empty()) {
				desc.HitGroupTable = {};
			} else {
				desc.HitGroupTable = {
					baseAddr + m_hitOffset,
					HitGroup.SectionSize(),
					HitGroup.RecordSize()
				};
			}

			if (Callable.Empty()) {
				desc.CallableShaderTable = {};
			} else {
				desc.CallableShaderTable = {
					baseAddr + m_callOffset,
					Callable.SectionSize(),
					Callable.RecordSize()
				};				
			}
		}

	private:
		UINT64 m_rayGenOffset = 0;
		UINT64 m_missOffset = 0;
		UINT64 m_hitOffset = 0;
		UINT64 m_callOffset = 0;
		UINT64 m_sbtSize = 0;
	};
}
