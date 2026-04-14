#include "TextureManager.h"

#include <d3dcompiler.h>

#include "State.h"

/**
 * @brief Provides access to the TextureManager singleton instance.
 *
 * @return TextureManager& Reference to the single TextureManager instance.
 */
TextureManager& TextureManager::GetSingleton()
{
	static TextureManager instance;
	return instance;
}

/**
 * @brief Prepares the texture manager's runtime resources.
 *
 * Populates the common texture cache with the set of application render targets and creates the shared downsample GPU resources (sampler, shaders, and downsample texture) required for generating mip-chained downsampled textures.
 */
void TextureManager::Initialize()
{
	CreateCommonTextures();
	CreateDownsampleResources();
}

/**
 * @brief Retrieve a cached common texture by name.
 *
 * @param name The key name of the texture in the common texture cache.
 * @return Texture* Pointer to the cached Texture if found, `nullptr` otherwise.
 */
TextureManager::Texture* TextureManager::GetCommonTexture(const std::string& name)
{
	auto it = commonTextureCache.find(name);
	if (it != commonTextureCache.end()) {
		return &it->second;
	}
	return nullptr;
}

/**
 * @brief Swap the cached textures for two named entries in the common texture cache.
 *
 * If both `name1` and `name2` exist in the cache, their associated Texture values are exchanged.
 * If either name is not found, the cache is left unchanged.
 *
 * @param name1 Key of the first texture in the common texture cache.
 * @param name2 Key of the second texture in the common texture cache.
 */
void TextureManager::SwapTextures(const std::string& name1, const std::string& name2)
{
	auto it1 = commonTextureCache.find(name1);
	auto it2 = commonTextureCache.find(name2);
	if (it1 != commonTextureCache.end() && it2 != commonTextureCache.end()) {
		std::swap(it1->second, it2->second);
	}
}

/**
 * @brief Populate the common texture cache with standard render-target and shader-resource textures used by the renderer.
 *
 * @details Reads the current screen size from globals::state and creates a set of named textures stored in commonTextureCache:
 * - Screen-sized render targets in multiple formats (HDR, SDR, various float/int channel formats) for temporary and final passes.
 * - Fixed-size bloom/lens targets (1024×1024) and a lens render target sized to the screen.
 * - Two 1×1 textures for luminance adaptation (ping/pong).
 * - A series of square fixed-size render targets (1024, 512, 256, 128, 64, 32, 16) intended for bloom/lens downsampling.
 *
 * Each created texture is inserted into the cache with a descriptive debug name.
 */
void TextureManager::CreateCommonTextures()
{
	auto state = globals::state;
	UINT screenWidth = static_cast<UINT>(state->screenSize.x);
	UINT screenHeight = static_cast<UINT>(state->screenSize.y);

	commonTextureCache.insert({ "TextureHDRTemp", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureHDRTemp") });
	commonTextureCache.insert({ "TextureHDRTemp2", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureHDRTemp2") });

	commonTextureCache.insert({ "RenderTargetRGBA32", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R8G8B8A8_UNORM, "TextureManager::RenderTargetRGBA32") });
	commonTextureCache.insert({ "RenderTargetRGBA64", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_UNORM, "TextureManager::RenderTargetRGBA64") });
	commonTextureCache.insert({ "RenderTargetRGBA64F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::RenderTargetRGBA64F") });
	commonTextureCache.insert({ "RenderTargetR16F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16_FLOAT, "TextureManager::RenderTargetR16F") });
	commonTextureCache.insert({ "RenderTargetR32F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R32_FLOAT, "TextureManager::RenderTargetR32F") });
	commonTextureCache.insert({ "RenderTargetRGB32F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R11G11B10_FLOAT, "TextureManager::RenderTargetRGB32F") });

	commonTextureCache.insert({ "TextureSDRTemp", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R10G10B10A2_UNORM, "TextureManager::TextureSDRTemp") });
	commonTextureCache.insert({ "TextureSDRTemp2", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R10G10B10A2_UNORM, "TextureManager::TextureSDRTemp2") });

	commonTextureCache.insert({ "TextureBloom", CreateTexture(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureBloom") });
	commonTextureCache.insert({ "TextureLens", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureLens") });

	commonTextureCache.insert({ "TextureBloomTemp", CreateTexture(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureBloomLensTemp") });

	commonTextureCache.insert({ "TextureAdaptation", CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "TextureManager::TextureAdaptation") });
	commonTextureCache.insert({ "TextureAdaptationSwap", CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "TextureManager::TextureAdaptationSwap") });

	// Create fixed-size render targets for bloom/lens
	std::vector<std::pair<std::string, UINT>> fixedSizes = {
		{ "RenderTarget1024", 1024 },
		{ "RenderTarget512", 512 },
		{ "RenderTarget256", 256 },
		{ "RenderTarget128", 128 },
		{ "RenderTarget64", 64 },
		{ "RenderTarget32", 32 },
		{ "RenderTarget16", 16 }
	};

	for (auto& [name, size] : fixedSizes) {
		commonTextureCache[name] = CreateTexture(size, size, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::" + name);
	}
}

/**
 * @brief Creates a GPU 2D texture with an associated render-target view and shader-resource view.
 *
 * Creates a single-level, single-array 2D texture sized width×height using the specified DXGI format
 * and returns a Texture containing the ID3D11Texture2D, its render-target view (RTV), and shader-resource view (SRV).
 * If `debugName` is non-empty, the D3D debug object name is assigned to the created texture.
 *
 * @param width Texture width in pixels.
 * @param height Texture height in pixels.
 * @param format DXGI format for the texture, RTV, and SRV.
 * @param debugName Optional debug name to set on the underlying D3D resource; pass an empty string to skip.
 * @return Texture Struct with `texture`, `rtv`, and `srv` members populated for the created resource.
 */
TextureManager::Texture TextureManager::CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName)
{
	TextureManager::Texture result;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&texDesc, nullptr, result.texture.put()));

	if (!debugName.empty()) {
		result.texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(debugName.length()), debugName.c_str());
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	DX::ThrowIfFailed(globals::d3d::device->CreateRenderTargetView(result.texture.get(), &rtvDesc, result.rtv.put()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(result.texture.get(), &srvDesc, result.srv.put()));

	return result;
}

/**
 * @brief Initializes GPU resources required for the downsample pipeline.
 *
 * Creates a linear sampler state, compiles and creates a fullscreen downsample vertex shader
 * and a pixel shader, and allocates the shared downsample texture used by the manager.
 *
 * On shader compilation failure the function logs the shader compiler output (if available)
 * and returns early without completing resource creation.
 *
 * The function initializes the following members: `linearSampler`, `downsampleVS`,
 * `downsamplePS`, and `sharedDownsampleTexture`.
 */
void TextureManager::CreateDownsampleResources()
{
	auto device = globals::d3d::device;

	// Create linear sampler
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));

	// Create downsample vertex shader (fullscreen triangle)
	const char* downsampleVertexShaderSource = R"HLSL(
	struct VS_INPUT_POST
	{
		float3 pos		: POSITION;
		float2 txcoord	: TEXCOORD0;
	};
	struct VS_OUTPUT_POST
	{
		float4 pos		: SV_POSITION;
		float2 txcoord0	: TEXCOORD0;
	};
	VS_OUTPUT_POST	main(VS_INPUT_POST IN)
	{
		VS_OUTPUT_POST	OUT;
		float4	pos;
		pos.xyz=IN.pos.xyz;
		pos.w=1.0;
		OUT.pos=pos;
		OUT.txcoord0.xy=IN.txcoord.xy;
		return OUT;
	}
	)HLSL";

	winrt::com_ptr<ID3DBlob> vertexShaderBlob;
	winrt::com_ptr<ID3DBlob> vertexErrorBlob;

	HRESULT vsResult = D3DCompile(
		downsampleVertexShaderSource,
		strlen(downsampleVertexShaderSource),
		nullptr,
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		0,
		0,
		vertexShaderBlob.put(),
		vertexErrorBlob.put());

	if (FAILED(vsResult)) {
		if (vertexErrorBlob) {
			logger::error("[TextureManager] Downsample vertex shader compilation failed: {}",
				static_cast<const char*>(vertexErrorBlob->GetBufferPointer()));
		}
		return;
	}

	DX::ThrowIfFailed(device->CreateVertexShader(
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize(),
		nullptr,
		downsampleVS.put()));

	// Create downsample pixel shader
	const char* downsamplePixelShaderSource = R"HLSL(
Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct VS_OUTPUT_POST
{
	float4 pos		: SV_POSITION;
	float2 txcoord0	: TEXCOORD0;
};

float4 main(VS_OUTPUT_POST IN) : SV_Target
{
	return SourceTexture.SampleLevel(LinearSampler, IN.txcoord0.xy, 0);
}
)HLSL";

	winrt::com_ptr<ID3DBlob> pixelShaderBlob;
	winrt::com_ptr<ID3DBlob> errorBlob;

	HRESULT result = D3DCompile(
		downsamplePixelShaderSource,
		strlen(downsamplePixelShaderSource),
		nullptr,
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		0,
		0,
		pixelShaderBlob.put(),
		errorBlob.put());

	if (FAILED(result)) {
		if (errorBlob) {
			logger::error("[TextureManager] Downsample shader compilation failed: {}",
				static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	DX::ThrowIfFailed(device->CreatePixelShader(
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize(),
		nullptr,
		downsamplePS.put()));

	// Create shared downsample texture
	sharedDownsampleTexture = CreateDownsampleTexture(DXGI_FORMAT_R11G11B10_FLOAT);
}

/**
 * @brief Creates a fixed 1024×1024 downsample texture with three mip levels for shared downsampling.
 *
 * Constructs a GPU texture configured as a render target and shader resource with 3 mip levels (1024, 512, 256),
 * creates an RTV for mip 0, and three SRVs: a full mip-chain SRV, a base-mip SRV (mip 0), and a "blurry" SRV (mip 2).
 * The created resources receive debug names and an informational log entry.
 *
 * @param format DXGI_FORMAT to use for the texture and its views.
 * @return DownsampleTexture Struct containing the created ID3D11Texture2D, its RTV, the SRV chain, the base SRV, and the blurry SRV.
 */
TextureManager::DownsampleTexture TextureManager::CreateDownsampleTexture(DXGI_FORMAT format)
{
	auto device = globals::d3d::device;

	DownsampleTexture fixedTexture;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 1024;
	texDesc.Height = 1024;
	texDesc.MipLevels = 3;  // 1024, 512, 256
	texDesc.ArraySize = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, fixedTexture.texture.put()));

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	DX::ThrowIfFailed(device->CreateRenderTargetView(fixedTexture.texture.get(), &rtvDesc, fixedTexture.rtv.put()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 3;
	srvDesc.Texture2D.MostDetailedMip = 0;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.get(), &srvDesc, fixedTexture.srvChain.put()));

	srvDesc.Texture2D.MipLevels = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.get(), &srvDesc, fixedTexture.srv.put()));

	srvDesc.Texture2D.MostDetailedMip = 2;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.get(), &srvDesc, fixedTexture.srvBlurry.put()));

	// Set debug names
	Util::SetResourceName(fixedTexture.texture.get(), "TextureManager::DownsampleTexture (1024x1024, 3 mips)");
	Util::SetResourceName(fixedTexture.rtv.get(), "TextureManager::DownsampleTexture RTV");
	Util::SetResourceName(fixedTexture.srvChain.get(), "TextureManager::DownsampleTexture SRV Chain");
	Util::SetResourceName(fixedTexture.srv.get(), "TextureManager::DownsampleTexture SRV 1024x1024");
	Util::SetResourceName(fixedTexture.srvBlurry.get(), "TextureManager::DownsampleTexture SRV 256x256");

	logger::info("[TextureManager] Created downsample texture: 1024x1024 with 3 mips (1024, 512, 256)");

	return fixedTexture;
}

/**
 * @brief Downsamples an input shader-resource texture into a fixed 1024×1024 downsample target and generates its mip chain.
 *
 * Binds the downsample vertex/pixel shaders and linear sampler, renders a full-screen pass into the target's RTV, then calls GenerateMips on the target's SRV chain to populate lower mip levels. The function returns immediately without performing work if any required resource is missing or if the provided `source` cannot be resolved to a 2D texture.
 *
 * @param source Shader-resource view of the source texture to downsample. Must reference a 2D texture.
 * @param texture Target DownsampleTexture whose RTV and SRV chain will be written and mipmapped.
 */
void TextureManager::DownsampleToFixed(ID3D11ShaderResourceView* source, DownsampleTexture& texture)
{
	if (!source || !texture.rtv || !downsampleVS || !downsamplePS || !linearSampler || !texture.srvChain) {
		return;
	}

	auto context = globals::d3d::context;

	// Get source texture description for calculating texel size
	winrt::com_ptr<ID3D11Resource> sourceResource;
	source->GetResource(sourceResource.put());
	if (!sourceResource) {
		return;
	}

	winrt::com_ptr<ID3D11Texture2D> sourceTexture;
	sourceResource.try_as(sourceTexture);
	if (!sourceTexture) {
		return;
	}

	D3D11_TEXTURE2D_DESC sourceDesc;
	sourceTexture->GetDesc(&sourceDesc);

	DownsampleCB constants{};
	constants.sourceTexelSizeX = 1.0f / static_cast<float>(sourceDesc.Width);
	constants.sourceTexelSizeY = 1.0f / static_cast<float>(sourceDesc.Height);

	// Set up render state for downsampling
	D3D11_VIEWPORT viewport = {};
	viewport.Width = 1024.0f;
	viewport.Height = 1024.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	context->RSSetViewports(1, &viewport);
	ID3D11RenderTargetView* rtvArray[] = { texture.rtv.get() };

	context->OMSetRenderTargets(1, rtvArray, nullptr);

	context->VSSetShader(downsampleVS.get(), nullptr, 0);
	context->PSSetShaderResources(0, 1, &source);

	ID3D11SamplerState* samplerArray[] = { linearSampler.get() };
	context->PSSetSamplers(0, 1, samplerArray);

	context->PSSetShader(downsamplePS.get(), nullptr, 0);

	context->Draw(4, 0);

	context->GenerateMips(texture.srvChain.get());
}

/**
 * @brief Updates the shared downsample texture from a source shader-resource view.
 *
 * Downsamples the provided source SRV into the manager's shared downsample texture and updates its mip chain.
 *
 * @param source Shader-resource view of the source texture to downsample; may be nullptr (no action).
 */
void TextureManager::UpdateDownsampledTexture(ID3D11ShaderResourceView* source)
{
	DownsampleToFixed(source, sharedDownsampleTexture);
}

/**
 * @brief Retrieves the shader-resource view for the shared downsample texture's base mip.
 *
 * @return ID3D11ShaderResourceView* Shader-resource view for the shared downsample texture at the base mip level, or `nullptr` if the resource has not been created.
 */
ID3D11ShaderResourceView* TextureManager::GetDownsampleTexture() const
{
	return sharedDownsampleTexture.srv.get();
}

/**
 * @brief Retrieves the shader-resource view that samples the blurry (higher-mip) version of the shared downsample texture.
 *
 * @return ID3D11ShaderResourceView* Pointer to the SRV for the blurry mip level of the shared downsample texture, or `nullptr` if not available.
 */
ID3D11ShaderResourceView* TextureManager::GetDownsampleTextureBlurry() const
{
	return sharedDownsampleTexture.srvBlurry.get();
}