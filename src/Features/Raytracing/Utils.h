#pragma once

#include "PCH.h"
#include "TruePBR.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include <directxpackedvector.h>

static inline uint PackUByte4(float4 unpacked)
{
	auto x = (uint)(unpacked.x * 255.0f) & 0xFF;
	auto y = (uint)(unpacked.y * 255.0f) & 0xFF;
	auto z = (uint)(unpacked.z * 255.0f) & 0xFF;
	auto w = (uint)(unpacked.w * 255.0f) & 0xFF;

	return (w << 24) | (z << 16) | (y << 8) | x;
}

static inline float4 UnpackUByte4(uint packed)
{
	float4 result;
	result.x = (packed & 0xFF) / 255.0f;
	result.y = ((packed >> 8) & 0xFF) / 255.0f;
	result.z = ((packed >> 16) & 0xFF) / 255.0f;
	result.w = (packed >> 24) / 255.0f;
	return result;
}

static inline uint PackByte4(float4 unpacked)
{
	return PackUByte4(unpacked * 0.5f + float4(0.5f, 0.5f, 0.5f, 0.5f));
}

static inline float4 UnpackByte4(uint packed)
{
	return UnpackUByte4(packed) * 2.0f - float4(1.0f, 1.0f, 1.0f, 1.0f);
}

static inline float3 Normalize(float3 vector)
{
	vector.Normalize();
	return vector;
}

static inline ID3D11Texture2D* TryGetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer)
{
	if (niPointer) {
		if (const auto& bsTexture = niPointer->rendererTexture; bsTexture) {
			return bsTexture->texture;
		}
	}

	return nullptr;
}

static inline DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform)
{
	DirectX::XMMATRIX temp;

	const RE::NiMatrix3& m = Transform.rotate;
	const float scale = Transform.scale;

	temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][0],
										   m.entry[1][0],
										   m.entry[2][0],
										   0.0f),
		scale);

	temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][1],
										   m.entry[1][1],
										   m.entry[2][1],
										   0.0f),
		scale);

	temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
										   m.entry[0][2],
										   m.entry[1][2],
										   m.entry[2][2],
										   0.0f),
		scale);

	temp.r[3] = DirectX::XMVectorSet(
		Transform.translate.x,
		Transform.translate.y,
		Transform.translate.z,
		1.0f);

	return temp;
}

static inline float3 Float3(const RE::NiPoint3& point3)
{
	return float3(point3.x, point3.y, point3.z);
}

static inline bool IsShareableFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_BC4_UNORM:
		return false;
		break;
	case DXGI_FORMAT_BC4_SNORM:
		return false;
		break;
	case DXGI_FORMAT_BC7_UNORM:
		return false;
		break;
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return false;
		break;
	default:
		return true;
		break;
	}
}

static inline DXGI_FORMAT GetCompatibleFormat(DXGI_FORMAT format, bool recompress)
{
	switch (format) {
	case DXGI_FORMAT_BC4_UNORM:
		return recompress ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_R8_UNORM;
		break;
	case DXGI_FORMAT_BC7_UNORM:
		return recompress ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
		break;
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return recompress ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		break;
	default:
		return format;
		break;
	}
}

static inline bool ShouldShareTexture(RE::BSTextureSet::Texture a_texture, bool pathTracing)
{
	if (a_texture == RE::BSTextureSet::Texture::kDiffuse)
		return true;

	if (pathTracing && a_texture == RE::BSTextureSet::Texture::kNormal)
		return true;

	if (globals::truePBR->currentTextureSet == nullptr) {
		if (a_texture == RE::BSTextureSet::Texture::kGlowMap)
			return true;
	} else {
		if (a_texture == BSLightingShaderMaterialPBR::EmissiveTexture)
			return true;

		if (pathTracing && a_texture == BSLightingShaderMaterialPBR::RmaosTexture)
			return true;
	}

	return false;
}

static inline std::string ToLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return s;
}

static inline bool ShareableTexture(const char* path)
{
	if (!path)
		return false;

	auto pathLower = ToLower(path);

	//if (pathLower.ends_with("_d.dds"))
	//	return true;

	if (pathLower.ends_with("_n.dds"))
		return false;

	if (pathLower.ends_with("_p.dds"))
		return false;

	if (pathLower.ends_with("_s.dds"))
		return false;

	if (pathLower.ends_with("_sk.dds"))
		return false;

	if (pathLower.ends_with("_msn.dds"))
		return false;

	if (pathLower.ends_with("_rmaos.dds"))
		return false;

	return true;
}

template <typename T>
static inline std::string GetFlagsString(auto value)
{
	using N = decltype(value);

	const auto& entries = magic_enum::enum_entries<T>();

	std::string flags;

	for (const auto& [flag, name] : entries) {
		if (value & static_cast<N>(flag)) {
			flags += fmt::format("{} ", name);
		}
	}

	return flags;
};

static uint32_t DivideRoundUp(uint32_t x, uint32_t divisor)
{
	return (x + divisor - 1) / divisor;
}

static uint32_t DivideRoundUp(uint32_t x, float divisor)
{
	return static_cast<uint32_t>(ceil(x / divisor));
}

static void CreateTexture2DUAV(ID3D12Device5* device, ID3D12Resource* resource, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
{
	auto desc = resource->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = desc.Format;

	device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handle);
}

static void CreateTexture2DSRV(ID3D12Device5* device, ID3D12Resource* resource, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
{
	auto desc = resource->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	device->CreateShaderResourceView(resource, &srvDesc, handle);
}

static inline float ShininessToRoughness(float shininess)
{
	// make sure shininess within valid range (0 - 1023), otherwise set to 1.0f
	if (shininess <= 0.0f || shininess > 1023.0f) {
		return 1.0f;
	}
	return std::pow(2.0f / (shininess + 2.0f), 0.25f);
}

template <class T>
static void detour_thunk(size_t offset)
{
	T::func = REL::Module::get().base() + offset;
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
	DetourTransactionCommit();
}