#include "Features/Skin.h"
#include "Globals.h"
#include "State.h"
#include <cmath>

// By PO3
// https://github.com/powerof3/Splashes-of-Skyrim/blob/master/src/Manager.cpp
float Skin::GetWaterHeight(const RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos)
{
	float waterHeight = -RE::NI_INFINITY;

	if (const auto waterManager = RE::TESWaterSystem::GetSingleton()) {
		waterHeight = a_ref->GetWaterHeight();

		if (waterHeight != -RE::NI_INFINITY) {
			return waterHeight;
		}

		const auto get_nearest_water_object_height = [&]() {
			for (const auto& waterObject : waterManager->waterObjects) {
				if (waterObject) {
					for (const auto& bound : waterObject->multiBounds) {
						if (bound) {
							if (auto size{ bound->size }; size.z <= 10.0f) {  //avoid sloped water
								auto center{ bound->center };
								const auto boundMin = center - size;
								const auto boundMax = center + size;
								if (!(a_pos.x < boundMin.x || a_pos.x > boundMax.x || a_pos.y < boundMin.y || a_pos.y > boundMax.y)) {
									return center.z;
								}
							}
						}
					}
				}
			}

			return -RE::NI_INFINITY;
		};

		waterHeight = get_nearest_water_object_height();
	}

	return waterHeight;
}

float4 Skin::GetWetness(RE::BSGeometry* geometry)
{
	float4 wetness = float4(0.0f, 0.0f, 0.0f, 0.0f);
	auto userData = geometry->GetUserData();
	if (userData && userData->IsActor()) {
		auto actor = static_cast<RE::Character*>(userData);
		const uint32_t actorFormID = userData->formID;
		const uint currentFrame = globals::state->frameCount;

		if (actorWetnessMap.size() > 1024) {
			actorWetnessMap.clear();
		}

		auto [it, inserted] = actorWetnessMap.try_emplace(actorFormID);
		auto& cached = it->second;
		if (!inserted && cached.frameCount == currentFrame) {
			return cached.wetness;
		}
		cached.frameCount = currentFrame;

		const float positionZ = actor->GetPositionZ();
		wetness.z = positionZ;
		const float stamina = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
		const float permanentStamina = actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kStamina);
		const float temporaryStamina = actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kStamina);
		const float maxStamina = std::max(permanentStamina + temporaryStamina, 1.0f);
		const float staminaPercentage = actor->IsDead() ? 1.0f : (stamina / maxStamina);
		const float sweatRange = settings.StartSweat - settings.FullSweat;
		wetness.x = (std::abs(sweatRange) < 1e-5f)             ? 0.0f :
		            (staminaPercentage >= settings.StartSweat) ? 0.0f :
		            (staminaPercentage <= settings.FullSweat)  ? 1.0f :
		                                                         (settings.StartSweat - staminaPercentage) / sweatRange;
		if (actor->IsInWater()) {
			wetness.y = 2.0f;
			const float waterHeight = GetWaterHeight(userData, actor->GetPosition());
			wetness.w = std::max(0.0f, waterHeight - positionZ);
		} else {
			wetness.y = 0.0f;
			wetness.w = 0.0f;
		}

		if (inserted) {
			cached.wetness = wetness;
		} else {
			const float fadeTime = std::max(settings.WetFadeTime, 0.001f);
			if (cached.wetness.x < wetness.x) {
				cached.wetness.x = wetness.x;
			} else if (cached.wetness.x > wetness.x) {
				cached.wetness.x -= *globals::game::deltaTime / fadeTime;
				cached.wetness.x = std::max(cached.wetness.x, 0.0f);
			}
			wetness.x = cached.wetness.x;

			if (cached.wetness.y < wetness.y) {
				cached.wetness.y = wetness.y;
				if (cached.wetness.w < wetness.w) {
					cached.wetness.w = wetness.w;
				} else {
					wetness.w = cached.wetness.w;
				}
			} else if (cached.wetness.y > wetness.y) {
				cached.wetness.y -= *globals::game::deltaTime / fadeTime;
				cached.wetness.y = std::max(cached.wetness.y, 0.0f);
				wetness.y = cached.wetness.y;
				if (wetness.y == 0.0f) {
					wetness.w = 0.0f;
					cached.wetness.w = 0.0f;
				} else if (cached.wetness.w < wetness.w) {
					cached.wetness.w = wetness.w;
				} else {
					wetness.w = cached.wetness.w;
				}
			} else if (cached.wetness.w < wetness.w) {
				cached.wetness.w = wetness.w;
			} else {
				wetness.w = cached.wetness.w;
			}
			cached.wetness = wetness;
		}
	}
	return wetness;
}
