#include "GrassCollision.h"

#include "State.h"
#include "Utils/ActorUtils.h"

static const uint MAX_BOUNDING_BOXES = 64;
static const uint MAX_COLLISIONS_PER_BOUNDING_BOX = 64;
static const uint MAX_COLLISIONS = MAX_BOUNDING_BOXES * MAX_COLLISIONS_PER_BOUNDING_BOX;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision,
	TrackRagdolls)

void GrassCollision::DrawSettings()
{
	if (ImGui::TreeNodeEx("Grass Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Grass Collision", (bool*)&settings.EnableGrassCollision);
		ImGui::TreePop();
	}
}

void GrassCollision::UpdateCollisions(PerFrame& perFrameData)
{
	eastl::vector<RE::Actor*> actorList{};

	// Actor query code from po3 under MIT
	// https://github.com/powerof3/PapyrusExtenderSSE/blob/7a73b47bc87331bec4e16f5f42f2dbc98b66c3a7/include/Papyrus/Functions/Faction.h#L24C7-L46
	if (const auto processLists = RE::ProcessLists::GetSingleton(); processLists) {
		std::vector<RE::BSTArray<RE::ActorHandle>*> actors;
		actors.push_back(&processLists->highActorHandles);  // High actors are in combat or doing something interesting
		for (auto array : actors) {
			for (auto& actorHandle : *array) {
				auto actorPtr = actorHandle.get();
				if (actorPtr && actorPtr.get()) {
					actorList.push_back(actorPtr.get());
				}
			}
		}
	}

	if (auto player = RE::PlayerCharacter::GetSingleton())
		actorList.push_back(player);

	RE::NiPoint3 cameraPosition = Util::GetEyePosition(0);

	// Sort actors by distance to eye, closest first
	std::sort(actorList.begin(), actorList.end(), [&cameraPosition](RE::Actor* a, RE::Actor* b) {
		float distA = cameraPosition.GetSquaredDistance(a->GetPosition());
		float distB = cameraPosition.GetSquaredDistance(b->GetPosition());
		return distA < distB;
	});

	eastl::vector<BoundingBoxPacked> boundingBoxData{};
	boundingBoxData.reserve(MAX_BOUNDING_BOXES);

	eastl::vector<float4> collisionsData{};
	collisionsData.reserve(MAX_COLLISIONS);

	uint collisionIndexExtent = 0;

	for (const auto actor : actorList) {
		if (actor && actor->Is3DLoaded()) {
			auto root = actor->Get3D(false);
			if (!root)
				continue;

			float distance = cameraPosition.GetDistance(actor->GetPosition());
			if (distance > 2048.0f)
				continue;

			eastl::vector<float4> collisionShapes{};

			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
				RE::NiPoint3 centerPos;
				float radius;
				if (Util::GetShapeBound(a_object, centerPos, radius)) {
					// Increase radius to pad around meshes
					radius *= 1.5f;

					// Cull extremely small collisions
					if (radius < distance * 0.001f)
						return RE::BSVisit::BSVisitControl::kContinue;

					centerPos -= cameraPosition;

					float4 data{};
					data.x = centerPos.x;
					data.y = centerPos.y;
					data.z = centerPos.z;
					data.w = radius;

					collisionShapes.push_back(data);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			std::sort(collisionShapes.begin(), collisionShapes.end(), [](const float4& a, const float4& b) {
				return a.w > b.w;
			});

			BoundingBoxPacked boundingBox;

			boundingBox.IndexStart = collisionIndexExtent;
			boundingBox.IndexEnd = collisionIndexExtent;

			uint boundingBoxCollisions = 0;

			for (const auto& data : collisionShapes) {
				collisionsData.push_back(data);

				float2 pointMin(data.x - data.w, data.y - data.w);
				float2 pointMax(data.x + data.w, data.y + data.w);

				boundingBox.MinExtent.x = std::min(boundingBox.MinExtent.x, pointMin.x);
				boundingBox.MinExtent.y = std::min(boundingBox.MinExtent.y, pointMin.y);

				boundingBox.MaxExtent.x = std::max(boundingBox.MaxExtent.x, pointMax.x);
				boundingBox.MaxExtent.y = std::max(boundingBox.MaxExtent.y, pointMax.y);

				boundingBox.IndexEnd++;

				boundingBoxCollisions++;

				if (boundingBoxCollisions == MAX_COLLISIONS_PER_BOUNDING_BOX)
					break;
			}

			if (boundingBox.IndexStart != boundingBox.IndexEnd)
				boundingBoxData.push_back(boundingBox);

			collisionIndexExtent = boundingBox.IndexEnd;
		}
	}

	perFrameData.BoundingBoxCount = std::min((uint)boundingBoxData.size(), MAX_BOUNDING_BOXES);

	auto context = globals::d3d::context;

	if (collisionIndexExtent > 0) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(collisionInstances->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(float4) * collisionIndexExtent;
		memcpy_s(mapped.pData, bytes, collisionsData.data(), bytes);
		context->Unmap(collisionInstances->resource.get(), 0);
	}

	if (perFrameData.BoundingBoxCount > 0) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(collisionBoundingBoxes->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(BoundingBoxPacked) * perFrameData.BoundingBoxCount;
		memcpy_s(mapped.pData, bytes, boundingBoxData.data(), bytes);
		context->Unmap(collisionBoundingBoxes->resource.get(), 0);
	}
}

void GrassCollision::Update()
{
	static Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		PerFrame perFrameData{};

		perFrameData.BoundingBoxCount = 0;

		static float2 prevCellID = { 0, 0 };

		auto eyePosNI = Util::GetEyePosition(0);
		static auto prevEyePosNI = eyePosNI;

		auto eyePos = float2{ eyePosNI.x, eyePosNI.y };

		float worldSize = 4096.0f;
		uint textureArrayDims = 512;

		float cellSize = worldSize / textureArrayDims;

		auto cellID = eyePos / cellSize;
		cellID = { round(cellID.x), round(cellID.y) };
		auto cellOrigin = cellID * cellSize;

		float2 cellIDDiff = prevCellID - cellID;
		prevCellID = cellID;

		perFrameData.PosOffset = cellOrigin - eyePos;

		perFrameData.ArrayOrigin = {
			((int)cellID.x - textureArrayDims / 2) % textureArrayDims,
			((int)cellID.y - textureArrayDims / 2) % textureArrayDims
		};

		perFrameData.ValidMargin = { (int)cellIDDiff.x, (int)cellIDDiff.y };

		auto calendar = RE::Calendar::GetSingleton();

		float currentGameTime = calendar->GetCurrentGameTime();
		static float lastGameTime = currentGameTime;

		perFrameData.TimeDelta = float((100000.0 / 1.16) * abs(double(currentGameTime) - double(lastGameTime)) / double(calendar->GetTimescale()));

		lastGameTime = currentGameTime;

		perFrameData.CameraHeightDelta = prevEyePosNI.z - eyePosNI.z;

		if (globals::game::isVR) {
			auto eyePosNIRight = Util::GetEyePosition(1);
			auto cameraRightDelta = eyePosNI - eyePosNIRight;
			perFrameData.CameraRightDelta = { cameraRightDelta.x, cameraRightDelta.y, cameraRightDelta.z };
		}

		if (settings.EnableGrassCollision)
			UpdateCollisions(perFrameData);

		perFrame->Update(perFrameData);

		UpdateCollisionTexture();

		prevCellID = cellID;
		prevEyePosNI = eyePosNI;

		auto context = globals::d3d::context;

		ID3D11Buffer* buffers[1];
		buffers[0] = perFrame->CB();
		context->VSSetConstantBuffers(5, ARRAYSIZE(buffers), buffers);

		ID3D11ShaderResourceView* srvs[] = { collisionTexture->srv.get() };
		context->VSSetShaderResources(100, ARRAYSIZE(srvs), srvs);
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
			.Format = DXGI_FORMAT_R16G16B16A16_UNORM,
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

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(BoundingBoxPacked);
		sbDesc.ByteWidth = sizeof(BoundingBoxPacked) * MAX_BOUNDING_BOXES;
		collisionBoundingBoxes = eastl::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_BOUNDING_BOXES;
		collisionBoundingBoxes->CreateSRV(srvDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(float4);
		sbDesc.ByteWidth = sizeof(float4) * MAX_COLLISIONS;
		collisionInstances = eastl::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_COLLISIONS;
		collisionInstances->CreateSRV(srvDesc);
	}
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

void GrassCollision::UpdateCollisionTexture()
{
	auto context = globals::d3d::context;

	if (!settings.EnableGrassCollision) {
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearUnorderedAccessViewFloat(collisionTexture->uav.get(), clearColor);
		return;
	}

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

	{
		ID3D11Buffer* buffers[1] = { perFrame->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);

		ID3D11ShaderResourceView* srvs[] = {
			collisionBoundingBoxes->srv.get(),
			collisionInstances->srv.get(),
		};

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { collisionTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetCollisionUpdateCS(), nullptr, 0);
		context->Dispatch(512 / 8, 512 / 8, 1);
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
}
