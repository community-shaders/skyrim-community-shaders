#include "GrassCollision.h"
#include "../Utils/ActorUtils.h"
#include "Deferred.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision,
	TrackRagdolls
)

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

		ImGui::Image(collisionTexture->srv.get(), { 512, 512 });

		ImGui::TreePop();
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
					for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
						data.centre[eyeIndex].x = centerPos.x;
						data.centre[eyeIndex].y = centerPos.y;
						data.centre[eyeIndex].z = centerPos.z;
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

		static float2 prevCellID = { 0, 0 };

		auto eyePosNI = Util::GetEyePosition(0);
		auto eyePos = float2{ eyePosNI.x, eyePosNI.y };

		float worldSize = 4096.0f;
		uint textureArrayDims = 512;

		float2 cellSize = {
			worldSize / textureArrayDims,
			worldSize / textureArrayDims
		};

		auto cellID = eyePos / cellSize;
		cellID = { round(cellID.x), round(cellID.y) };
		auto cellOrigin = cellID * cellSize;

		// float2 cellIDDiff = prevCellID - cellID;
		prevCellID = cellID;

		perFrameData.PosOffset = cellOrigin - eyePos;
		perFrameData.ArrayOrigin = {
			((int)cellID.x - textureArrayDims / 2) % textureArrayDims,
			((int)cellID.y - textureArrayDims / 2) % textureArrayDims
		};
		perFrameData.eyePosition = { eyePosNI.x, eyePosNI.y, eyePosNI.z };
		perFrameData.timeDelta = *globals::game::deltaTime;

		if (settings.EnableGrassCollision)
			UpdateCollisions(perFrameData);

		perFrame->Update(perFrameData);

		UpdateCollision();

		prevCellID = cellID;

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

	{
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = 512,
			.Height = 512,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16_UNORM,
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
	}
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
}

ID3D11ComputeShader* GrassCollision::GetCollisionUpdateCS()
{
	if (!collisionUpdateCS) {
		logger::debug("Compiling CollisionUpdateCS");
		collisionUpdateCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GrassCollision\\CollisionUpdateCS.hlsl", {}, "cs_5_0"));
	}
	return collisionUpdateCS;
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

	ID3D11UnorderedAccessView* uavs[] = { collisionTexture->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(GetCollisionUpdateCS(), nullptr, 0);
	context->Dispatch(512 / 8, 512 / 8, 1);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);

	ID3D11ShaderResourceView* srvs[] = { collisionTexture->srv.get() };
	context->VSSetShaderResources(100, ARRAYSIZE(srvs), srvs);

	context->VSSetSamplers(0, 1, &globals::deferred->linearSampler);
}
