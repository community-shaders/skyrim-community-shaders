#pragma once

#include <EASTL/memory.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstring>
#include <d3d12.h>
#include <dxcapi.h>
#include "Features/RaytracedGI/HeapManager.h"

namespace DX12
{
	class RTPipelineBuilder
	{
	public:
		enum ExportType
		{
			RayGeneration,
			Miss,
			Hit
		};

		void AddRayGenLib(IDxcBlob* shaderBlob, const eastl::wstring& exportName, const eastl::wstring& renameFrom = L"main")
		{
			AddLibrary(shaderBlob, exportName, renameFrom, ExportType::RayGeneration);
		}

		void AddMissLib(IDxcBlob* shaderBlob, const eastl::wstring& exportName, const eastl::wstring& renameFrom = L"main")
		{
			AddLibrary(shaderBlob, exportName, renameFrom, ExportType::Miss);
		}

		void AddHitLib(IDxcBlob* shaderBlob, const eastl::wstring& exportName, const eastl::wstring& renameFrom = L"main")
		{
			AddLibrary(shaderBlob, exportName, renameFrom, ExportType::Hit);
		}

		// Add a DXIL library (shader blob) with exports
		void AddLibrary(IDxcBlob* shaderBlob, const eastl::wstring& exportName, const eastl::wstring& renameFrom, const ExportType& exportType)
		{
			// Store export string
			exportedNames.push_back(eastl::make_unique<eastl::wstring>(exportName));
			renameFromNames.push_back(eastl::make_unique<eastl::wstring>(renameFrom));

			if (exportType == ExportType::RayGeneration)
				rayGenNames.push_back(exportName);
			else if (exportType == ExportType::Miss)
				missNames.push_back(exportName);

			// Prepare export descriptor
			dxilExportStorage.emplace_back(eastl::make_unique<D3D12_EXPORT_DESC>(
				exportedNames.back()->c_str(),
				renameFrom.empty() ? nullptr : renameFromNames.back()->c_str(),
				D3D12_EXPORT_FLAG_NONE
			));

			// Store DXIL library descriptor
			dxilLibStorage.emplace_back(eastl::make_unique<D3D12_DXIL_LIBRARY_DESC>(
				D3D12_SHADER_BYTECODE{
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
				},
				1,
				dxilExportStorage.back().get() 
			));

			// Subobject
			subobjects.push_back({
				.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY,
				.pDesc = dxilLibStorage.back().get() 
			});
		}

		// Add a hit group
		void AddHitGroup(const eastl::wstring& hitGroupName, const eastl::wstring& closestHit = L"", const eastl::wstring& anyHit = L"", const eastl::wstring& intersection = L"")
		{
			// Store hit group name for lifetime
			hitGroupNames.push_back(hitGroupName);

			closestHitNames.push_back(closestHit);
			anyHitNames.push_back(anyHit);
			intersectionNames.push_back(intersection);

			hitGroupStorage.emplace_back(eastl::make_unique<D3D12_HIT_GROUP_DESC>(
				hitGroupNames.back().c_str(),
				D3D12_HIT_GROUP_TYPE_TRIANGLES,
				anyHit.empty() ? nullptr : anyHitNames.back().c_str(),
				closestHit.empty() ? nullptr : closestHitNames.back().c_str(),
				intersection.empty() ? nullptr : intersectionNames.back().c_str()
			));

			subobjects.push_back({
				.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP,
				.pDesc = hitGroupStorage.back().get() 
			});
		}

		// Shader config
		void AddShaderConfig(UINT maxPayloadSizeInBytes, UINT maxAttributeSizeInBytes)
		{
			shaderConfigStorage.emplace_back(eastl::make_unique<D3D12_RAYTRACING_SHADER_CONFIG>(maxPayloadSizeInBytes, maxAttributeSizeInBytes));

			subobjects.push_back({
				.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
				.pDesc = shaderConfigStorage.back().get() 
			});
		}

		// Global root signature
		void AddGlobalRootSignature(ID3D12RootSignature* rootSignature)
		{
			globalRootStorage.emplace_back(eastl::make_unique<D3D12_GLOBAL_ROOT_SIGNATURE>(rootSignature));

			subobjects.push_back({
				.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
				.pDesc = globalRootStorage.back().get() 
			});
		}

		// Pipeline config
		void AddPipelineConfig(UINT maxRecursion)
		{
			pipelineConfigStorage.emplace_back(eastl::make_unique<D3D12_RAYTRACING_PIPELINE_CONFIG>(maxRecursion));

			subobjects.push_back({
				.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
				.pDesc = pipelineConfigStorage.back().get() 
			});
		}

		// Build final state object descriptor
		D3D12_STATE_OBJECT_DESC* MakeStateObjectDesc(D3D12_STATE_OBJECT_TYPE type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
		{
			stateObjectDesc = eastl::make_unique<D3D12_STATE_OBJECT_DESC>(
				type,
				static_cast<UINT>(subobjects.size()),
				subobjects.data()
			);

			return stateObjectDesc.get();
		}

		// D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES
		// D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
		// 
		// D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT

		static size_t Align(size_t size, size_t alignment)
		{
			return (size + alignment - 1) & ~(alignment - 1);
		}

		size_t ShaderTablesSize()
		{
			size_t size = 0;

			auto writeTable = [&](const eastl::vector<eastl::wstring>& names) {
				size_t tableSize = 0; 

				for (size_t i = 0; i < names.size(); i++) {
					tableSize += Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
				}

				size += Align(tableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
			};

			writeTable(rayGenNames);
			writeTable(missNames);
			writeTable(hitGroupNames);

			return size;
		}

		ShaderTable WriteShaderIdentifiers(ID3D12StateObjectProperties* pipelineProps, void* mappedData) const
		{
			ShaderTable shaderTable{};

			uint8_t* startPtr = static_cast<uint8_t*>(mappedData);

			uint8_t* ptr = startPtr;

			auto writeTable = [&](const eastl::vector<eastl::wstring>& names) {
				const size_t shaderIDSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

				uint8_t* tableStartPtr = ptr;

				for (const auto& name : names) {
					
					void* shaderID = pipelineProps->GetShaderIdentifier(name.c_str());
					memcpy(ptr, shaderID, shaderIDSize);

					ptr += Align(shaderIDSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
				}

				const size_t tableSize = ptr - tableStartPtr;
				const size_t tableSizeAligned = Align(tableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
				ptr = tableStartPtr + tableSizeAligned;

				return tableSizeAligned;
			};

			shaderTable.RayGenerationShaderRecord = { 0, writeTable(rayGenNames) };
			shaderTable.MissShaderTable = { static_cast<UINT64>(ptr - startPtr), writeTable(missNames), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT };
			shaderTable.HitGroupTable = { static_cast<UINT64>(ptr - startPtr), writeTable(hitGroupNames), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT };

			return shaderTable;
		}



	private:
		// Storage for lifetime management
		eastl::vector<eastl::unique_ptr<eastl::wstring>> renameFromNames;
		eastl::vector<eastl::unique_ptr<eastl::wstring>> exportedNames;

		eastl::vector<eastl::wstring> rayGenNames;
		eastl::vector<eastl::wstring> missNames;
		eastl::vector<eastl::wstring> hitGroupNames;

		eastl::vector<eastl::wstring> closestHitNames;
		eastl::vector<eastl::wstring> anyHitNames;
		eastl::vector<eastl::wstring> intersectionNames;

		eastl::vector<eastl::unique_ptr<D3D12_EXPORT_DESC>> dxilExportStorage;
		eastl::vector<eastl::unique_ptr<D3D12_DXIL_LIBRARY_DESC>> dxilLibStorage;
		eastl::vector<eastl::unique_ptr<D3D12_HIT_GROUP_DESC>> hitGroupStorage;
		eastl::vector<eastl::unique_ptr<D3D12_RAYTRACING_SHADER_CONFIG>> shaderConfigStorage;
		eastl::vector<eastl::unique_ptr<D3D12_GLOBAL_ROOT_SIGNATURE>> globalRootStorage;
		eastl::vector<eastl::unique_ptr<D3D12_RAYTRACING_PIPELINE_CONFIG>> pipelineConfigStorage;
		eastl::vector<D3D12_STATE_SUBOBJECT> subobjects;

		eastl::unique_ptr<D3D12_STATE_OBJECT_DESC> stateObjectDesc = nullptr;
	};
}
