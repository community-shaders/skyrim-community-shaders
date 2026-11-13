#include "Textures.h"

#include "../Editor/Files.h"
#include "Globals.h"

#include <DirectXTex.h>

namespace SkinTextures
{
	namespace
	{
		DirectX::TexMetadata Metadata(std::span<const uint8_t> a_bytes)
		{
			DirectX::TexMetadata metadata{};
			if (FAILED(DirectX::GetMetadataFromDDSMemory(a_bytes.data(), a_bytes.size(), DirectX::DDS_FLAGS_NONE, metadata)) ||
				metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.IsCubemap() ||
				metadata.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || metadata.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
				throw std::runtime_error(T("feature.skin.expected_a_single_2d_dds_texture", "Expected a single 2D DDS texture."));
			switch (DirectX::MakeLinear(metadata.format)) {
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_BC1_UNORM:
			case DXGI_FORMAT_BC2_UNORM:
			case DXGI_FORMAT_BC3_UNORM:
			case DXGI_FORMAT_BC5_UNORM:
			case DXGI_FORMAT_BC7_UNORM:
				break;
			default:
				throw std::runtime_error(T("feature.skin.dds_format", "Use RGBA8, BC1/2/3/7, or BC5 UNORM for detail normals without a mask."));
			}
			return metadata;
		}
	}

	DXGI_FORMAT Validate(const std::string& a_path)
	{
		if (!a_path.empty()) {
			const auto bytes = SkinEditor::ReadResource(a_path);
			const auto metadata = Metadata(bytes);
			DirectX::ScratchImage image;
			if (FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, image)))
				throw std::runtime_error(T("feature.skin.dds_decoding_failed", "DDS decoding failed."));
			return metadata.format;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	Resource Load(const std::string& a_path)
	{
		Resource result;
		if (a_path.empty())
			return result;
		try {
			const auto bytes = SkinEditor::ReadResource(a_path);
			const auto metadata = Metadata(bytes);
			DirectX::ScratchImage image;
			if (FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, image)))
				throw std::runtime_error(T("feature.skin.dds_decoding_failed", "DDS decoding failed."));
			image.OverrideFormat(DirectX::MakeLinear(metadata.format));
			if (FAILED(DirectX::CreateShaderResourceView(globals::d3d::device, image.GetImages(), image.GetImageCount(), image.GetMetadata(), result.view.put())))
				throw std::runtime_error(T("feature.skin.dds_resource_creation_failed", "DDS resource creation failed."));
			result.fingerprint = SkinEditor::Fingerprint(bytes);
			result.reconstructNormal = metadata.format == DXGI_FORMAT_BC5_UNORM;
			if (metadata.format == DXGI_FORMAT_BC5_SNORM)
				throw std::runtime_error(T("feature.skin.use_unsigned_bc5_or_an_rgb_normal_texture", "Use unsigned BC5 or an RGB normal texture."));
		} catch (const std::exception& e) {
			result.view = nullptr;
			result.error = e.what();
			logger::warn("[Advanced Skin] {}: {}", a_path, result.error);
		}
		return result;
	}
}
