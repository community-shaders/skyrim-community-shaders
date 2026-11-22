#include "TextureTools.h"

#include "Globals.h"

#include <DirectXTex.h>
#include <cmath>

namespace SkinEditor
{
	namespace
	{
		void Check(HRESULT a_result)
		{
			if (FAILED(a_result))
				throw std::runtime_error(I18n::GetSingleton()->Format("feature.skin.texture_operation_failed", { { "code", std::format("{:08X}", static_cast<uint32_t>(a_result)) } }, "Texture operation failed ({code})."));
		}
		DirectX::ScratchImage ReadImage(const std::string& a_path, DXGI_FORMAT* a_sourceFormat = nullptr)
		{
			using namespace DirectX;
			const auto bytes = ReadResource(a_path);
			ScratchImage image;
			HRESULT result = LoadFromDDSMemory(bytes.data(), bytes.size(), DDS_FLAGS_NONE, nullptr, image);
			if (FAILED(result))
				result = LoadFromTGAMemory(bytes.data(), bytes.size(), TGA_FLAGS_NONE, nullptr, image);
			if (FAILED(result))
				result = LoadFromWICMemory(bytes.data(), bytes.size(), WIC_FLAGS_IGNORE_SRGB, nullptr, image);
			Check(result);
			const auto& metadata = image.GetMetadata();
			if (metadata.dimension != TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.IsCubemap() ||
				metadata.width > 8192 || metadata.height > 8192)
				throw std::runtime_error(T("feature.skin.use_a_single_2d_image_no_larger_than_8192_by", "Use a single 2D image, no larger than 8192 by 8192."));
			if (a_sourceFormat)
				*a_sourceFormat = metadata.format;
			image.OverrideFormat(MakeLinear(metadata.format));
			ScratchImage pixels;
			if (IsCompressed(image.GetMetadata().format))
				Check(Decompress(*image.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, pixels));
			else if (image.GetMetadata().format == DXGI_FORMAT_R8G8B8A8_UNORM)
				Check(pixels.InitializeFromImage(*image.GetImage(0, 0, 0)));
			else
				Check(Convert(*image.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, 0.5f, pixels));
			return pixels;
		}
		Bytes Encode(DirectX::ScratchImage& a_pixels, bool a_compress)
		{
			using namespace DirectX;
			ScratchImage mips;
			Check(GenerateMipMaps(*a_pixels.GetImage(0, 0, 0), TEX_FILTER_FANT | TEX_FILTER_SEPARATE_ALPHA, 0, mips));
			if (a_compress) {
				ScratchImage encoded;
				Check(Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DXGI_FORMAT_BC7_UNORM,
					TEX_COMPRESS_PARALLEL, 1.0f, encoded));
				mips = std::move(encoded);
			}
			Blob blob;
			Check(SaveToDDSMemory(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DDS_FLAGS_FORCE_DX10_EXT, blob));
			const auto* data = static_cast<const uint8_t*>(blob.GetBufferPointer());
			return { data, data + blob.GetBufferSize() };
		}
	}

	Bytes PackControls(const TextureTools& a_options)
	{
		std::array<DirectX::ScratchImage, 4> inputs;
		size_t width = 0, height = 0;
		for (size_t i = 0; i < inputs.size(); ++i) {
			if (a_options.channels[i].empty())
				continue;
			inputs[i] = ReadImage(a_options.channels[i]);
			const auto& metadata = inputs[i].GetMetadata();
			if (width && (width != metadata.width || height != metadata.height))
				throw std::runtime_error(T("feature.skin.all_control_maps_must_have_the_same_dimensions_no_automatic", "All control maps must have the same dimensions. No automatic UV rescaling is performed."));
			width = metadata.width;
			height = metadata.height;
		}
		DirectX::ScratchImage pixels;
		Check(pixels.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width ? width : 4, height ? height : 4, 1, 1));
		auto* image = pixels.GetImage(0, 0, 0);
		for (size_t y = 0; y < image->height; ++y)
			for (size_t x = 0; x < image->width; ++x)
				for (size_t channel = 0; channel < inputs.size(); ++channel) {
					const auto* input = inputs[channel].GetImage(0, 0, 0);
					image->pixels[y * image->rowPitch + x * 4 + channel] = input ? input->pixels[y * input->rowPitch + x * 4] :
					                                                               static_cast<uint8_t>(std::clamp(a_options.defaults[channel], 0.0f, 1.0f) * 255 + 0.5f);
				}
		return Encode(pixels, a_options.compress);
	}

	Bytes PackDetail(const TextureTools& a_options)
	{
		DXGI_FORMAT sourceFormat{};
		auto pixels = ReadImage(a_options.normal, &sourceFormat);
		const bool reconstruct = sourceFormat == DXGI_FORMAT_BC5_UNORM;
		DirectX::ScratchImage mask;
		if (!a_options.mask.empty())
			mask = ReadImage(a_options.mask);
		auto* image = pixels.GetImage(0, 0, 0);
		const auto* region = mask.GetImage(0, 0, 0);
		if (region && (region->width != image->width || region->height != image->height))
			throw std::runtime_error(T("feature.skin.the_detail_normal_and_uv_region_mask_must_have_the", "The detail normal and UV-region mask must have the same image dimensions."));
		for (size_t y = 0; y < image->height; ++y)
			for (size_t x = 0; x < image->width; ++x) {
				if (reconstruct) {
					auto* pixel = image->pixels + y * image->rowPitch + x * 4;
					const float nx = pixel[0] / 127.5f - 1.f, ny = pixel[1] / 127.5f - 1.f;
					pixel[2] = static_cast<uint8_t>((std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny)) * 0.5f + 0.5f) * 255.f + 0.5f);
				}
				image->pixels[y * image->rowPitch + x * 4 + 3] = region ? region->pixels[y * region->rowPitch + x * 4] : 255;
			}
		return Encode(pixels, a_options.compress);
	}

	SkinTextures::Resource ChannelPreview(const std::string& a_path, int a_channel)
	{
		auto pixels = ReadImage(a_path);
		DirectX::ScratchImage thumbnail;
		Check(DirectX::Resize(*pixels.GetImage(0, 0, 0), 256, 256, DirectX::TEX_FILTER_FANT, thumbnail));
		auto* image = thumbnail.GetImage(0, 0, 0);
		for (size_t y = 0; y < image->height; ++y)
			for (size_t x = 0; x < image->width; ++x) {
				auto* pixel = image->pixels + y * image->rowPitch + x * 4;
				if (a_channel > 0 && a_channel <= 4)
					pixel[0] = pixel[1] = pixel[2] = pixel[a_channel - 1];
				pixel[3] = 255;
			}
		SkinTextures::Resource result;
		Check(DirectX::CreateShaderResourceView(globals::d3d::device, thumbnail.GetImages(), thumbnail.GetImageCount(), thumbnail.GetMetadata(), result.view.put()));
		return result;
	}

	void CheckTextures(const SkinMaterials::Material& a_material)
	{
		SkinMaterials::Validate(a_material);
		for (size_t i = 0; i < a_material.textures.size(); ++i) {
			if (a_material.textures[i].empty())
				continue;
			const auto path = SkinMaterials::NormalizeTexturePath(a_material.textures[i]);
			const auto format = SkinTextures::Validate(path);
			if (format == DXGI_FORMAT_BC5_SNORM || (i != 2 && format == DXGI_FORMAT_BC5_UNORM))
				throw std::runtime_error(T("feature.skin.bc5_unorm_is_only_supported_for_detail_normals_without_a", "BC5 UNORM is only supported for detail normals without a mask."));
		}
	}
}
