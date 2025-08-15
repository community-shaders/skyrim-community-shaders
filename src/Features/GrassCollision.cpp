#include "GrassCollision.h"
#include "../Utils/ActorUtils.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision,
	TrackRagdolls)

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
			ImGui::Text("If enabled, dead actors (ragdolls) will be tracked and shown in the table.");
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Active/Total Actors : {}/{}", activeActorCount, totalActorCount).c_str());
		ImGui::Text(std::format("Total Collisions : {}", currentCollisionCount).c_str());

		std::vector<std::string> headers = { "Name/EditorID", "FormID", "Type", "X", "Y", "Z", "SqDist" };
		std::vector<ActorRow> sortableRows;
		RE::NiPoint3 eyePos = Util::GetAverageEyePosition();
		RE::NiPoint3 playerPos{};
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			playerPos = player->GetPosition();
		}
		for (const auto ref : actorList) {
			Util::ActorDisplayInfo info;
			if (!Util::GetActorDisplayInfo(ref, eyePos, settings.TrackRagdolls, info))
				continue;
			float offsetSqDist = (info.pos).GetSquaredDistance(playerPos);
			std::vector<std::string> row = {
				info.name,
				info.formID,
				info.type,
				std::format("{:.2f}", info.pos.x),
				std::format("{:.2f}", info.pos.y),
				std::format("{:.2f}", info.pos.z),
				std::format("{:.2f}", offsetSqDist)
			};
			sortableRows.push_back(ActorRow{ info.actor, row, offsetSqDist });
		}
		ImGui::BeginChild("GrassCollidersTableChild", ImVec2(0, 300), true);
		Util::ShowSortedStringTableCustom<ActorRow>(
			"GrassCollidersTable",
			headers,
			sortableRows,
			6,  // Default sort by sqDist
			true,
			{
				// Custom comparators for each column
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? a.row[0] < b.row[0] : a.row[0] > b.row[0]; },                                              // Name
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? a.row[1] < b.row[1] : a.row[1] > b.row[1]; },                                              // FormID
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? a.row[2] < b.row[2] : a.row[2] > b.row[2]; },                                              // Type
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? std::stof(a.row[3]) < std::stof(b.row[3]) : std::stof(a.row[3]) > std::stof(b.row[3]); },  // X
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? std::stof(a.row[4]) < std::stof(b.row[4]) : std::stof(a.row[4]) > std::stof(b.row[4]); },  // Y
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? std::stof(a.row[5]) < std::stof(b.row[5]) : std::stof(a.row[5]) > std::stof(b.row[5]); },  // Z
				[](const ActorRow& a, const ActorRow& b, bool asc) { return asc ? a.sqDist < b.sqDist : a.sqDist > b.sqDist; }                                               // SqDist
			},
			[](int, int colIdx, const ActorRow& actorRow) {
				if (colIdx == 0) {
					if (ImGui::Selectable(actorRow.row[colIdx].c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
						// Teleport player to actor
						if (auto player = RE::PlayerCharacter::GetSingleton()) {
							auto actor = static_cast<RE::Actor*>(actorRow.actor);
							if (actor) {
								RE::NiPoint3 dest = actor->GetPosition();
								player->SetPosition(dest, true);
							}
						}
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Teleport to this actor's position.");
					}
				} else {
					ImGui::TextUnformatted(actorRow.row[colIdx].c_str());
				}
			});
		ImGui::EndChild();
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
	globals::features::grassCollision.Update();
	func(This, Pass, RenderFlags);
}
