#include "GrassCollision.h"

#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision)

void GrassCollision::DrawSettings()
{
	if (ImGui::TreeNodeEx("Grass Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Grass Collision", (bool*)&settings.EnableGrassCollision);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Allows player collision to modify grass position.");
		}

		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Active/Total Actors : {}/{}", activeActorCount, totalActorCount).c_str());
		ImGui::Text(std::format("Total Collisions : {}", currentCollisionCount).c_str());
		ImGui::TreePop();
	}
}

static bool GetShapeBound(RE::NiAVObject* a_node, RE::NiPoint3& centerPos, float& radius)
{
	RE::bhkNiCollisionObject* Colliedobj = nullptr;
	if (a_node->collisionObject)
		Colliedobj = a_node->collisionObject->AsBhkNiCollisionObject();

	if (!Colliedobj)
		return false;

	RE::bhkRigidBody* bhkRigid = Colliedobj->body.get() ? Colliedobj->body.get()->AsBhkRigidBody() : nullptr;
	RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
	if (bhkRigid && hkpRigid) {
		RE::hkVector4 massCenter;
		bhkRigid->GetCenterOfMassWorld(massCenter);
		float massTrans[4];
		_mm_store_ps(massTrans, massCenter.quad);
		centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();

		const RE::hkpShape* shape = hkpRigid->collidable.GetShape();
		if (shape && shape->type == RE::hkpShapeType::kCapsule) {
			float upExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float downExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, -1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto z_extent = (upExtent + downExtent) / 2.0f;

			float forwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float backwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, -1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto y_extent = (forwardExtent + backwardExtent) / 2.0f;

			float leftExtent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float rightExtent = shape->GetMaximumProjection(RE::hkVector4{ -1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto x_extent = (leftExtent + rightExtent) / 2.0f;

			radius = sqrtf(x_extent * x_extent + y_extent * y_extent + z_extent * z_extent);

			return true;
		}
	}

	return false;
}

static bool GetShapeBound(RE::bhkNiCollisionObject* Colliedobj, RE::NiPoint3& centerPos, float& radius)
{
	if (!Colliedobj)
		return false;

	RE::bhkRigidBody* bhkRigid = Colliedobj->body.get() ? Colliedobj->body.get()->AsBhkRigidBody() : nullptr;
	RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
	if (bhkRigid && hkpRigid) {
		RE::hkVector4 massCenter;
		bhkRigid->GetCenterOfMassWorld(massCenter);
		float massTrans[4];
		_mm_store_ps(massTrans, massCenter.quad);
		centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();

		const RE::hkpShape* shape = hkpRigid->collidable.GetShape();
		if (shape && shape->type == RE::hkpShapeType::kCapsule) {
			float upExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float downExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, -1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto z_extent = (upExtent + downExtent) / 2.0f;

			float forwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float backwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, -1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto y_extent = (forwardExtent + backwardExtent) / 2.0f;

			float leftExtent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float rightExtent = shape->GetMaximumProjection(RE::hkVector4{ -1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto x_extent = (leftExtent + rightExtent) / 2.0f;

			radius = sqrtf(x_extent * x_extent + y_extent * y_extent + z_extent * z_extent);

			return true;
		}
	}

	return false;
}

void GrassCollision::UpdateCollisions(PerFrame& perFrameData)
{
	actorList.clear();

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
		if (currentCollisionCount == 256)
			break;
		if (auto root = actor->Get3D(false)) {
			auto position = actor->GetPosition();
			float distance = cameraPosition.GetDistance(position);
			if (distance > 2048.0f)  // Cull against distance
				continue;

			activeActorCount++;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
				RE::NiPoint3 centerPos;
				float radius;
				if (GetShapeBound(a_object, centerPos, radius)) {
					if (radius < distance * 0.01f)
						return RE::BSVisit::BSVisitControl::kContinue;
					radius *= 2.0f;
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

		if (settings.EnableGrassCollision)
			UpdateCollisions(perFrameData);

		perFrame->Update(perFrameData);

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
	globals::features::grassCollision->Update();
	func(This, Pass, RenderFlags);
}
