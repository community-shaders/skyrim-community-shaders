#include "GrassCollision.h"
#include "../Utils/ActorUtils.h"
#include "State.h"
#include "Deferred.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision,
	TrackRagdolls,
	EnableBlur,
	BlurRadius)

struct ActorRow
{
	RE::TESObjectREFR* actor;
	std::vector<std::string> row;
	float sqDist;
};

void GrassCollision::DrawSettings()
{
	if (ImGui::TreeNodeEx("Grass Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Grass Collision", (bool*)&settings.EnableGrassCollision);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Allows player collision to modify grass position.");
		}
		ImGui::Checkbox("Track Ragdolls", &settings.TrackRagdolls);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("If enabled, dead actors (ragdolls) will be tracked.");
		}
		ImGui::Checkbox("Enable Blur", &settings.EnableBlur);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Applies blur to the collision texture for smoother grass transitions.");
		}
		if (settings.EnableBlur) {
			ImGui::SliderFloat("Blur Radius", &settings.BlurRadius, 0.5f, 5.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Controls the blur intensity. Higher values create smoother but less precise collision areas.");
			}
		}
		ImGui::TreePop();

		ImGui::Image(collisionTexture->srv.get(), { 512, 512 });
	}
}

void GrassCollision::UpdateCollisions(PerFrame& perFrameData)
{
	actorList.clear();
	std::vector<Util::ActorDisplayInfo> actorDisplayInfos;

	// Actor query code from po3 under MIT
	// https://github.com/powerof3/PapyrusExtenderSSE/blob/7a73b47bc87331bec4e16f5f42f2dbc98b66c3a7/include/Papyrus/Functions/Faction.h#L24C7-L46
	if (const auto processLists = RE::ProcessLists::GetSingleton(); processLists) {
		std::vector<RE::BSTArray<RE::ActorHandle>*> actors;
		actors.push_back(&processLists->highActorHandles);  // High actors are in combat or doing something interesting
		for (auto array : actors) {
			for (auto& actorHandle : *array) {
				auto actorPtr = actorHandle.get();
				if (actorPtr && actorPtr.get() && actorPtr.get()->Is3DLoaded()) {
					actorList.push_back(actorPtr.get());
					totalActorCount++;
				}
			}
		}
	}

	if (auto player = RE::PlayerCharacter::GetSingleton())
		actorList.push_back(player);

	RE::NiPoint3 cameraPosition = Util::GetAverageEyePosition();

	for (const auto actor : actorList) {
		Util::ActorDisplayInfo info;
		if (!Util::GetActorDisplayInfo(actor, cameraPosition, settings.TrackRagdolls, info))
			continue;
		actorDisplayInfos.push_back(info);
	}

	for (const auto& info : actorDisplayInfos) {
		if (currentCollisionCount == 256)
			break;
		auto actor = static_cast<RE::Actor*>(info.actor);
		if (actor && actor->Is3DLoaded()) {
			auto root = actor->Get3D(false);
			if (!root)
				continue;
			float distance = cameraPosition.GetDistance(info.pos);
			if (distance > 2048.0f)
				continue;
			activeActorCount++;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
				RE::NiPoint3 centerPos;
				float radius;
				if (Util::GetShapeBound(a_object, centerPos, radius)) {
					if (radius < distance * 0.01f)
						return RE::BSVisit::BSVisitControl::kContinue;
					CollisionData data{};
					RE::NiPoint3 eyePosition{};
					for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
						eyePosition = Util::GetEyePosition(eyeIndex);
						data.centre[eyeIndex].x = centerPos.x - eyePosition.x;
						data.centre[eyeIndex].y = centerPos.y - eyePosition.y;
						data.centre[eyeIndex].z = centerPos.z - eyePosition.z;
					}
					data.centre[0].w = radius;
					perFrameData.collisionData[currentCollisionCount] = data;
					currentCollisionCount++;
					if (currentCollisionCount == 256)
						return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
	}
	perFrameData.numCollisions = currentCollisionCount;
}

void GrassCollision::Update()
{
	if (updatePerFrame) {
		PerFrame perFrameData{};

		perFrameData.numCollisions = 0;
		currentCollisionCount = 0;
		totalActorCount = 0;
		activeActorCount = 0;
		
		auto cameraPosition = Util::GetAverageEyePosition();


		// Calculate texel size in world space
		float worldSize = 2048.0f;
		float textureSize = 4096.0f;;
		float texelWorldSize = worldSize / textureSize;

		// Snap camera position to texel grid
		DirectX::XMFLOAT2 desiredCenter(cameraPosition.x, cameraPosition.y);

		// Snap to texel boundaries
		DirectX::XMFLOAT2 snappedCenter;
		snappedCenter.x = floor(desiredCenter.x / texelWorldSize) * texelWorldSize;
		snappedCenter.y = floor(desiredCenter.y / texelWorldSize) * texelWorldSize;
		
		static auto previousSnappedCenter = snappedCenter;

		// Calculate how many texels the clipmap shifted
		DirectX::XMFLOAT2 centerDelta;
		centerDelta.x = snappedCenter.x - previousSnappedCenter.x;
		centerDelta.y = snappedCenter.y - previousSnappedCenter.y;

		DirectX::XMINT2 texelOffset;
		texelOffset.x = static_cast<int>(round(centerDelta.x / texelWorldSize));
		texelOffset.y = static_cast<int>(round(centerDelta.y / texelWorldSize));

		// First frame initialization
		static bool firstFrame = true;
		if (firstFrame) {
			previousSnappedCenter = snappedCenter;
			texelOffset.x = 0;
			texelOffset.y = 0;
			firstFrame = false;
		}

		perFrameData.currentCenter = snappedCenter;
		perFrameData.previousCenter = previousSnappedCenter;
		perFrameData.worldSize = worldSize;
		perFrameData.timeDelta = *globals::game::deltaTime;
		perFrameData.textureDimensions = DirectX::XMUINT2(4096, 4096);
		perFrameData.texelOffset = texelOffset;

		if (settings.EnableGrassCollision)
			UpdateCollisions(perFrameData);

		perFrame->Update(perFrameData);

		UpdateCollision();

		ApplyBlur();

		previousSnappedCenter = snappedCenter;

		updatePerFrame = false;
	}

	auto context = globals::d3d::context;

	static Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		ID3D11Buffer* buffers[1];
		buffers[0] = perFrame->CB();
		context->VSSetConstantBuffers(5, ARRAYSIZE(buffers), buffers);
	}
}

void GrassCollision::LoadSettings(json& o_json)
{
	settings = o_json;
}

void GrassCollision::SaveSettings(json& o_json)
{
	o_json = settings;
}

void GrassCollision::RestoreDefaultSettings()
{
	settings = {};
}

void GrassCollision::PostPostLoad()
{
	Hooks::Install();
}

void GrassCollision::SetupResources()
{
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>());
	collisionUpdateCB = new ConstantBuffer(ConstantBufferDesc<CollisionUpdateCB>());
	blurCB = new ConstantBuffer(ConstantBufferDesc<BlurCB>());

	D3D11_TEXTURE2D_DESC texDesc = {
		.Width = 4096,
		.Height = 4096,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32G32_FLOAT,
		.SampleDesc = { .Count = 1 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	};

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	collisionTexture = new Texture2D(texDesc);
	collisionTexture->CreateSRV(srvDesc);
	collisionTexture->CreateUAV(uavDesc);

	collisionTextureSwap = new Texture2D(texDesc);
	collisionTextureSwap->CreateSRV(srvDesc);
	collisionTextureSwap->CreateUAV(uavDesc);

	blurredCollisionTexture = new Texture2D(texDesc);
	blurredCollisionTexture->CreateSRV(srvDesc);
	blurredCollisionTexture->CreateUAV(uavDesc);
}

void GrassCollision::Reset()
{
	updatePerFrame = true;
}

bool GrassCollision::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Grass:
		return true;
	default:
		return false;
	}
}

void GrassCollision::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::grassCollision.Update();
	func(This, Pass, RenderFlags);
}

void GrassCollision::ClearShaderCache()
{
	if (collisionUpdateCS)
		collisionUpdateCS->Release();
	collisionUpdateCS = nullptr;
	if (blurCS)
		blurCS->Release();
	blurCS = nullptr;
}

ID3D11ComputeShader* GrassCollision::GetCollisionUpdateCS()
{
	if (!collisionUpdateCS) {
		logger::debug("Compiling CollisionUpdateCS");
		collisionUpdateCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GrassCollision\\CollisionUpdateCS.hlsl", {}, "cs_5_0"));
	}
	return collisionUpdateCS;
}

ID3D11ComputeShader* GrassCollision::GetBlurCS()
{
	if (!blurCS) {
		logger::debug("Compiling BlurCS");
		blurCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GrassCollision\\BlurCS.hlsl", {}, "cs_5_0"));
	}
	return blurCS;
}

void GrassCollision::ApplyBlur()
{
	if (!settings.EnableBlur)
		return;

	auto context = globals::d3d::context;

	BlurCB blurData{};
	blurData.textureDimensions = DirectX::XMUINT2(4096, 4096);
	blurData.blurRadius = settings.BlurRadius;

	auto currentOutput = useCollisionSwap ? collisionTextureSwap : collisionTexture;

	// Two-pass blur: horizontal then vertical
	for (uint32_t pass = 0; pass < 2; pass++) {
		blurData.blurDirection = pass; // 0 = horizontal, 1 = vertical
		blurCB->Update(blurData);

		ID3D11Buffer* buffers[1] = { blurCB->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);

		// Input texture
		auto inputTex = (pass == 0) ? currentOutput : blurredCollisionTexture;
		ID3D11ShaderResourceView* srvs[] = { inputTex->srv.get() };
		context->CSSetShaderResources(0, 1, srvs);

		// Output texture
		auto outputTex = (pass == 0) ? blurredCollisionTexture : currentOutput;
		ID3D11UnorderedAccessView* uavs[] = { outputTex->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		context->CSSetSamplers(0, 1, &globals::deferred->linearSampler);
		context->CSSetShader(GetBlurCS(), nullptr, 0);
		context->Dispatch(4096 / 8, 4096 / 8, 1);

		// Clear resources
		ID3D11ShaderResourceView* null_srvs[1] = { nullptr };
		context->CSSetShaderResources(0, 1, null_srvs);
		ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
	}

	context->CSSetShader(nullptr, nullptr, 0);
	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	auto srv = blurredCollisionTexture->srv.get();
	context->VSSetShaderResources(100, 1, &srv);
}

void GrassCollision::UpdateCollision()
{
	auto context = globals::d3d::context;

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	ID3D11Buffer* buffers[1] = { perFrame->CB() };
	context->CSSetConstantBuffers(0, 1, buffers);

	auto inputCollision = useCollisionSwap ? collisionTextureSwap : collisionTexture;
	auto outputCollision = !useCollisionSwap ? collisionTextureSwap : collisionTexture;

	useCollisionSwap = !useCollisionSwap;

	ID3D11ShaderResourceView* srvs[] = { inputCollision->srv.get() };
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	ID3D11UnorderedAccessView* uavs[] = { outputCollision->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSGetSamplers(0, 1, &globals::deferred->linearSampler);

	context->CSSetShader(GetCollisionUpdateCS(), nullptr, 0);
	context->Dispatch(4096 / 8, 4096 / 8, 1);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[1] = { nullptr };
	context->CSSetShaderResources(0, 1, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);

	auto srv = outputCollision->srv.get();
	context->VSSetShaderResources(100, 1, &srv);

	context->VSSetSamplers(0, 1, &globals::deferred->linearSampler);


}
			