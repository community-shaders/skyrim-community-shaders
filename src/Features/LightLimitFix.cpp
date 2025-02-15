#include "LightLimitFix.h"

#include "Shadercache.h"
#include "State.h"

static constexpr uint CLUSTER_MAX_LIGHTS = 256;
static constexpr uint MAX_LIGHTS = 1024;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightLimitFix::Settings,
	EnableContactShadows,
	EnableParticleLights,
	EnableParticleLightsCulling,
	EnableParticleLightsDetection,
	ParticleLightsSaturation,
	EnableParticleLightsOptimization,
	ParticleBrightness,
	ParticleRadius,
	BillboardBrightness,
	BillboardRadius)

void LightLimitFix::DrawSettings()
{
	if (ImGui::TreeNodeEx("Particle Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables Particle Lights.");
		}

		ImGui::Checkbox("Enable Culling", &settings.EnableParticleLightsCulling);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Significantly improves performance by not rendering empty textures. Only disable if you are encountering issues.");
		}

		ImGui::Checkbox("Enable Detection", &settings.EnableParticleLightsDetection);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Adds particle lights to the player light level, so that NPCs can detect them for stealth and gameplay.");
		}

		ImGui::Checkbox("Enable Optimization", &settings.EnableParticleLightsOptimization);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Merges vertices which are close enough to each other to improve performance.");
		}

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::TextWrapped("Particle Lights Customisation");
		ImGui::SliderFloat("Saturation", &settings.ParticleLightsSaturation, 1.0, 2.0, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Particle light saturation.");
		}
		ImGui::SliderFloat("Particle Brightness", &settings.ParticleBrightness, 0.0, 10.0, "%.2f");
		ImGui::SliderFloat("Particle Radius", &settings.ParticleRadius, 0.0, 10.0, "%.2f");
		ImGui::SliderFloat("Billboard Brightness", &settings.BillboardBrightness, 0.0, 10.0, "%.2f");
		ImGui::SliderFloat("Billboard Radius", &settings.BillboardRadius, 0.0, 10.0, "%.2f");

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Contact Shadows", &settings.EnableContactShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("All lights cast small shadows. Performance impact.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	auto shaderCache = globals::shaderCache;

	if (ImGui::TreeNodeEx("Light Limit Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Lights Visualisation", &settings.EnableLightsVisualisation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables visualization of the light limit\n");
		}

		{
			static const char* comboOptions[] = { "Light Limit", "Strict Lights Count", "Clustered Lights Count", "Shadow Mask" };
			ImGui::Combo("Lights Visualisation Mode", (int*)&settings.LightsVisualisationMode, comboOptions, 4);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					" - Visualise the light limit. Red when the \"strict\" light limit is reached (portal-strict lights).\n"
					" - Visualise the number of strict lights.\n"
					" - Visualise the number of clustered lights.\n"
					" - Visualize the Shadow Mask.\n");
			}
		}
		currentEnableLightsVisualisation = settings.EnableLightsVisualisation;
		if (previousEnableLightsVisualisation != currentEnableLightsVisualisation) {
			globals::state->SetDefines(settings.EnableLightsVisualisation ? "LLFDEBUG" : "");
			shaderCache->Clear(RE::BSShader::Type::Lighting);
			previousEnableLightsVisualisation = currentEnableLightsVisualisation;
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Clustered Light Count : {}", lightCount).c_str());
		ImGui::Text(std::format("Particle Lights Count : {}", currentParticleLights.size()).c_str());

		ImGui::TreePop();
	}
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableContactShadows = settings.EnableContactShadows;
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	std::copy(clusterSize, clusterSize + 3, perFrame.ClusterSize);
	return perFrame;
}

void LightLimitFix::CleanupParticleLights(RE::NiNode* a_node)
{
	particleLightsReferences.erase(a_node);
}

void LightLimitFix::SetupResources()
{
	auto screenSize = Util::ConvertToDynamic(globals::state->screenSize);
	if (REL::Module::IsVR())
		screenSize.x *= .5;
	clusterSize[0] = ((uint)screenSize.x + 63) / 64;
	clusterSize[1] = ((uint)screenSize.y + 63) / 64;
	clusterSize[2] = 32;
	uint clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

	{
		std::string clusterSizeStrs[3];
		for (int i = 0; i < 3; ++i)
			clusterSizeStrs[i] = std::format("{}", clusterSize[i]);

		std::vector<std::pair<const char*, const char*>> defines = {
			{ "CLUSTER_BUILDING_DISPATCH_SIZE_X", clusterSizeStrs[0].c_str() },
			{ "CLUSTER_BUILDING_DISPATCH_SIZE_Y", clusterSizeStrs[1].c_str() },
			{ "CLUSTER_BUILDING_DISPATCH_SIZE_Z", clusterSizeStrs[2].c_str() }
		};

		clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", defines, "cs_5_0");
		clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", defines, "cs_5_0");

		lightBuildingCB = new ConstantBuffer(ConstantBufferDesc<LightBuildingCB>());
		lightCullingCB = new ConstantBuffer(ConstantBufferDesc<LightCullingCB>());
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = clusterCount;

		sbDesc.StructureByteStride = sizeof(ClusterAABB);
		sbDesc.ByteWidth = sizeof(ClusterAABB) * numElements;
		clusters = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexCounter = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexList = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightGrid->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(LightData);
		sbDesc.ByteWidth = sizeof(LightData) * MAX_LIGHTS;
		lights = eastl::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_LIGHTS;
		lights->CreateSRV(srvDesc);
	}

	{
		strictLightDataCB = new ConstantBuffer(ConstantBufferDesc<StrictLightDataCB>());
	}
}

void LightLimitFix::Reset()
{
	for (auto& particleLight : currentParticleLights) {
		if (!particleLight.billboard) {
			if (const auto particleSystem = static_cast<RE::NiParticleSystem*>(particleLight.node)) {
				if (auto particleData = particleSystem->GetParticleRuntimeData().particleData.get()) {
					particleData->DecRefCount();
				}
			}
		}
		particleLight.node->DecRefCount();
	}
	currentParticleLights.clear();
	std::swap(currentParticleLights, queuedParticleLights);
}

void LightLimitFix::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LightLimitFix::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

RE::NiNode* GetParentRoomNode(RE::NiAVObject* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	static const auto* roomRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSMultiBoundRoom }.get();
	static const auto* portalRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSPortalSharedNode }.get();

	const auto* rtti = object->GetRTTI();
	if (rtti == roomRtti || rtti == portalRtti) {
		return static_cast<RE::NiNode*>(object);
	}

	return GetParentRoomNode(object->parent);
}

void LightLimitFix::BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	strictLightDataTemp.NumStrictLights = 0;

	strictLightDataTemp.RoomIndex = -1;
	if (!roomNodes.empty()) {
		if (RE::NiNode* roomNode = GetParentRoomNode(a_pass->geometry)) {
			if (auto it = roomNodes.find(roomNode); it != roomNodes.cend()) {
				strictLightDataTemp.RoomIndex = it->second;
			}
		}
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass, DirectX::XMMATRIX&, uint32_t, uint32_t, float, Space)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == globals::game::smState->shadowSceneNode[0];

	strictLightDataTemp.NumStrictLights = inWorld ? 0 : (a_pass->numLights - 1);

	for (uint32_t i = 0; i < strictLightDataTemp.NumStrictLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		auto niLight = bsLight->light.get();

		auto& runtimeData = niLight->GetLightRuntimeData();

		LightData light{};
		light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
		light.color *= runtimeData.fade;
		light.color *= bsLight->lodDimmer;

		light.radius = runtimeData.radius.x;

		SetLightPosition(light, niLight->world.translate, inWorld);

		light.lightFlags = static_cast<LightFlags>(runtimeData.ambient.red);

		if (bsLight->IsShadowLight()) {
			auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
			GET_INSTANCE_MEMBER(shadowLightIndex, shadowLight);
			light.shadowMaskIndex = shadowLightIndex;
			light.lightFlags.set(LightFlags::Shadow);
		}

		strictLightDataTemp.StrictLights[i] = light;
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	auto shaderCache = globals::shaderCache;
	auto context = globals::d3d::context;
	auto smState = globals::game::smState;

	if (!shaderCache->IsEnabled())
		return;

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();

	auto shadowSceneNode = smState->shadowSceneNode[0];

	const bool isEmpty = strictLightDataTemp.NumStrictLights == 0;
	const bool isWorld = accumulator->GetRuntimeData().activeShadowSceneNode == shadowSceneNode;
	const int roomIndex = strictLightDataTemp.RoomIndex;

	if (!isEmpty || (isEmpty && !wasEmpty) || isWorld != wasWorld || previousRoomIndex != roomIndex) {
		strictLightDataCB->Update(strictLightDataTemp);
		wasEmpty = isEmpty;
		wasWorld = isWorld;
		previousRoomIndex = roomIndex;
	}

	if (frameChecker.IsNewFrame()) {
		ID3D11Buffer* buffer = { strictLightDataCB->CB() };
		context->PSSetConstantBuffers(3, 1, &buffer);
	}
}

void LightLimitFix::SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached)
{
	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		RE::NiPoint3 eyePosition;
		Matrix viewMatrix;

		if (a_cached) {
			eyePosition = eyePositionCached[eyeIndex];
			viewMatrix = viewMatrixCached[eyeIndex];
		} else {
			eyePosition = Util::GetEyePosition(eyeIndex);
			viewMatrix = Util::GetCameraData(eyeIndex).viewMat;
		}

		auto worldPos = a_initialPosition - eyePosition;
		a_light.positionWS[eyeIndex].data.x = worldPos.x;
		a_light.positionWS[eyeIndex].data.y = worldPos.y;
		a_light.positionWS[eyeIndex].data.z = worldPos.z;
		a_light.positionVS[eyeIndex].data = DirectX::SimpleMath::Vector3::Transform(a_light.positionWS[eyeIndex].data, viewMatrix);
	}
}

float LightLimitFix::CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point)
{
	// See BSLight::CalculateLuminance_14131D3D0
	// Performs lighting on the CPU which is identical to GPU code

	auto lightDirection = light.position - point;
	float lightDist = lightDirection.Length();
	float intensityFactor = std::clamp(lightDist / light.radius, 0.0f, 1.0f);
	float intensityMultiplier = 1 - intensityFactor * intensityFactor;

	return light.grey * intensityMultiplier;
}

void LightLimitFix::AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
	int particleLightsDetectionHits = 0;
	if (settings.EnableParticleLightsDetection) {
		for (auto& light : cachedParticleLights) {
			auto luminance = CalculateLuminance(light, targetPosition);
			lightLevel += luminance;
			if (luminance > 0.0)
				particleLightsDetectionHits++;
		}
	}
	numHits += particleLightsDetectionHits;
}

void LightLimitFix::Prepass()
{
	auto context = globals::d3d::context;

	UpdateLights();

	ID3D11ShaderResourceView* views[3]{};
	views[0] = lights->srv.get();
	views[1] = lightIndexList->srv.get();
	views[2] = lightGrid->srv.get();
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);
}

bool LightLimitFix::IsValidLight(RE::BSLight* a_light)
{
	return a_light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool LightLimitFix::IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

struct VertexColor
{
	std::uint8_t data[4];
};

struct VertexPosition
{
	std::uint8_t data[3];
};

std::string ExtractTextureStem(std::string_view a_path)
{
	if (a_path.size() < 1)
		return {};

	auto lastSeparatorPos = a_path.find_last_of("\\/");
	if (lastSeparatorPos == std::string::npos)
		return {};

	a_path = a_path.substr(lastSeparatorPos + 1);
	a_path.remove_suffix(4);  // Remove ".dds"

	auto textureNameView = a_path | std::views::transform([](auto c) { return (char)::tolower(c); });
	std::string textureName = { textureNameView.begin(), textureNameView.end() };

	return textureName;
}

LightLimitFix::ParticleLightReference LightLimitFix::GetParticleLightConfigs(RE::BSRenderPass* a_pass)
{
	auto particleLights = globals::features::llf::particleLights;

	// see https://www.nexusmods.com/skyrimspecialedition/articles/1391
	if (settings.EnableParticleLights) {
		if (auto shaderProperty = netimmerse_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty)) {
			if (!shaderProperty->lightData) {
				if (auto material = shaderProperty->GetMaterial()) {
					// Check if it's a valid particle light
					bool billboard = false;
					if (!netimmerse_cast<RE::NiParticleSystem*>(a_pass->geometry)) {
						if (auto parent = a_pass->geometry->parent) {
							if (auto billboardNode = netimmerse_cast<RE::NiBillboardNode*>(parent)) {
								billboard = true;
							} else {
								return { false };
							}
						} else {
							return { false };
						}
					}

					// Already scanned
					{
						auto it = particleLightsReferences.find(reinterpret_cast<RE::NiNode*>(a_pass->geometry));
						if (it != particleLightsReferences.end())
							return (*it).second;
					}

					// Not scanned, scan now

					if (!material->sourceTexturePath.empty()) {
						std::string textureName = ExtractTextureStem(material->sourceTexturePath.c_str());
						if (textureName.size() < 1) {
							particleLightsReferences.insert({ (RE::NiNode*)a_pass->geometry, { false } });
							return { false };
						}

						auto& configs = particleLights->particleLightConfigs;
						auto it = configs.find(textureName);
						if (it == configs.end()) {
							particleLightsReferences.insert({ (RE::NiNode*)a_pass->geometry, { false } });
							return { false };
						}

						ParticleLights::Config* config = &it->second;
						ParticleLights::GradientConfig* gradientConfig = nullptr;
						if (!material->greyscaleTexturePath.empty()) {
							textureName = ExtractTextureStem(material->greyscaleTexturePath.c_str());
							if (textureName.size() < 1) {
								particleLightsReferences.insert({ (RE::NiNode*)a_pass->geometry, { false } });
								return { false };
							}

							auto& gradientConfigs = particleLights->particleLightGradientConfigs;
							auto itGradient = gradientConfigs.find(textureName);
							if (itGradient == gradientConfigs.end()) {
								particleLightsReferences.insert({ (RE::NiNode*)a_pass->geometry, { false } });
								return { false };
							}
							gradientConfig = &itGradient->second;
						}

						ParticleLightReference reference{ true };
						reference.billboard = billboard;
						reference.config = config;
						reference.gradientConfig = gradientConfig;
						reference.baseColor = { 1, 1, 1, 1 };

						if (billboard) {
							if (auto rendererData = a_pass->geometry->GetGeometryRuntimeData().rendererData) {
								if (auto triShape = a_pass->geometry->AsTriShape()) {
									uint32_t vertexSize = rendererData->vertexDesc.GetSize();
									if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS)) {
										uint32_t offset = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR);

										uint8_t maxAlpha = 0u;
										VertexColor* vertexColor = nullptr;

										for (int v = 0; v < triShape->GetTrishapeRuntimeData().vertexCount; v++) {
											if (VertexColor* vertex = reinterpret_cast<VertexColor*>(&rendererData->rawVertexData[vertexSize * v + offset])) {
												uint8_t alpha = vertex->data[3];
												if (alpha > maxAlpha) {
													maxAlpha = alpha;
													vertexColor = vertex;
												}
											}
										}

										if (vertexColor) {
											reference.baseColor.red *= vertexColor->data[0] / 255.f;
											reference.baseColor.green *= vertexColor->data[1] / 255.f;
											reference.baseColor.blue *= vertexColor->data[2] / 255.f;
											if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha)) {
												reference.baseColor.alpha *= vertexColor->data[3] / 255.f;
											}
										}
									}
								}
							}
						}

						particleLightsReferences.insert({ reinterpret_cast<RE::NiNode*>(a_pass->geometry), reference });
						return reference;
					}
				}
			}
		}
	}
	return { false };
}

bool LightLimitFix::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return true;

	auto reference = GetParticleLightConfigs(a_pass);
	if (reference.valid) {
		if (AddParticleLight(a_pass, reference)) {
			return !(settings.EnableParticleLightsCulling && reference.config->cull);
		}
	}
	return true;
}

bool LightLimitFix::AddParticleLight(RE::BSRenderPass* a_pass, ParticleLightReference a_reference)
{
	auto shaderProperty = static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty);
	auto material = shaderProperty->GetMaterial();
	auto config = a_reference.config;
	auto gradientConfig = a_reference.gradientConfig;

	a_pass->geometry->IncRefCount();

	if (!a_reference.billboard) {
		if (auto particleSystem = static_cast<RE::NiParticleSystem*>(a_pass->geometry)) {
			if (auto particleData = particleSystem->GetParticleRuntimeData().particleData.get()) {
				particleData->IncRefCount();
			}
		}
	}

	RE::NiColorA color = a_reference.baseColor;
	color.red *= material->baseColor.red * material->baseColorScale;
	color.green *= material->baseColor.green * material->baseColorScale;
	color.blue *= material->baseColor.blue * material->baseColorScale;
	color.alpha *= material->baseColor.alpha * shaderProperty->alpha;

	if (auto emittance = shaderProperty->unk88) {
		color.red *= emittance->red;
		color.green *= emittance->green;
		color.blue *= emittance->blue;
	}

	if (gradientConfig) {
		auto grey = float3(config->colorMult.red, config->colorMult.green, config->colorMult.blue).Dot(float3(0.3f, 0.59f, 0.11f));
		color.red *= grey * gradientConfig->color.red;
		color.green *= grey * gradientConfig->color.green;
		color.blue *= grey * gradientConfig->color.blue;
	} else {
		color.red *= config->colorMult.red;
		color.green *= config->colorMult.green;
		color.blue *= config->colorMult.blue;
	}

	color.alpha = config->radiusMult;

	ParticleLightInfo info;
	info.billboard = a_reference.billboard;
	info.node = a_pass->geometry;
	info.color = color;

	queuedParticleLights.push_back(info);
	return true;
}

void LightLimitFix::PostPostLoad()
{
	globals::features::llf::particleLights->GetConfigs();
	Hooks::Install();
}

void LightLimitFix::DataLoaded()
{
	auto iMagicLightMaxCount = globals::game::gameSettingCollection->GetSetting("iMagicLightMaxCount");
	iMagicLightMaxCount->data.i = MAXINT32;
	logger::info("[LLF] Unlocked magic light limit");
}

float LightLimitFix::CalculateLightDistance(float3 a_lightPosition, float a_radius)
{
	return (a_lightPosition.x * a_lightPosition.x) + (a_lightPosition.y * a_lightPosition.y) + (a_lightPosition.z * a_lightPosition.z) - (a_radius * a_radius);
}

void LightLimitFix::AddCachedParticleLights(eastl::vector<LightData>& lightsData, LightLimitFix::LightData& light)
{
	static float& lightFadeStart = *reinterpret_cast<float*>(REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = *reinterpret_cast<float*>(REL::RelocationID(527669, 414583).address());

	float distance = CalculateLightDistance(light.positionWS[0].data, light.radius);

	float dimmer = 0.0f;

	if (distance < lightFadeStart || lightFadeEnd == 0.0f) {
		dimmer = 1.0f;
	} else if (distance <= lightFadeEnd) {
		dimmer = 1.0f - ((distance - lightFadeStart) / (lightFadeEnd - lightFadeStart));
	} else {
		dimmer = 0.0f;
	}

	light.color *= dimmer;

	if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
		for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++)
			light.positionVS[eyeIndex].data = DirectX::SimpleMath::Vector3::Transform(light.positionWS[eyeIndex].data, viewMatrixCached[eyeIndex]);

		lightsData.push_back(light);

		CachedParticleLight cachedParticleLight{};
		cachedParticleLight.grey = float3(light.color.x, light.color.y, light.color.z).Dot(float3(0.3f, 0.59f, 0.11f));
		cachedParticleLight.radius = light.radius;
		cachedParticleLight.position = { light.positionWS[0].data.x + eyePositionCached[0].x, light.positionWS[0].data.y + eyePositionCached[0].y, light.positionWS[0].data.z + eyePositionCached[0].z };

		cachedParticleLights.push_back(cachedParticleLight);
	}
}

float3 LightLimitFix::Saturation(float3 color, float saturation)
{
	float grey = color.Dot(float3(0.3f, 0.59f, 0.11f));
	color.x = std::max(std::lerp(grey, color.x, saturation), 0.0f);
	color.y = std::max(std::lerp(grey, color.y, saturation), 0.0f);
	color.z = std::max(std::lerp(grey, color.z, saturation), 0.0f);
	return color;
}

namespace RE
{
	class BSMultiBoundRoom : public NiNode
	{};
}

void LightLimitFix::UpdateLights()
{
	auto smState = globals::game::smState;

	lightsNear = *globals::game::cameraNear;
	lightsFar = *globals::game::cameraFar;

	auto shadowSceneNode = smState->shadowSceneNode[0];

	// Cache data since cameraData can become invalid in first-person

	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		eyePositionCached[eyeIndex] = Util::GetEyePosition(eyeIndex);
		viewMatrixCached[eyeIndex] = Util::GetCameraData(eyeIndex).viewMat;
		viewMatrixCached[eyeIndex].Invert(viewMatrixInverseCached[eyeIndex]);
	}

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);

	// Process point lights

	roomNodes.empty();

	auto addRoom = [&](RE::NiNode* node, LightData& light) {
		uint8_t roomIndex = 0;
		if (auto it = roomNodes.find(node); it == roomNodes.cend()) {
			roomIndex = static_cast<uint8_t>(roomNodes.size());
			roomNodes.insert_or_assign(node, roomIndex);
		} else {
			roomIndex = it->second;
		}
		light.roomFlags.SetBit(roomIndex, 1);
	};

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.color *= runtimeData.fade;
					light.color *= bsLight->lodDimmer;

					light.radius = runtimeData.radius.x;

					light.lightFlags = static_cast<LightFlags>(runtimeData.ambient.red);

					if (!IsGlobalLight(bsLight)) {
						// List of BSMultiBoundRooms affected by a light
						for (const auto& roomPtr : bsLight->rooms) {
							addRoom(roomPtr, light);
						}
						// List of BSPortals affected by a light
						for (const auto& portalPtr : bsLight->portals) {
							addRoom(portalPtr->portalSharedNode.get(), light);
						}
						light.lightFlags.set(LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						GET_INSTANCE_MEMBER(shadowLightIndex, shadowLight);
						light.shadowMaskIndex = shadowLightIndex;
						light.lightFlags.set(LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						SetLightPosition(light, niLight->world.translate);

						if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	for (auto& e : shadowSceneNode->GetRuntimeData().activeLights) {
		addLight(e);
	}
	for (auto& e : shadowSceneNode->GetRuntimeData().activeShadowLights) {
		addLight(e);
	}

	{
		std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
		cachedParticleLights.clear();

		LightData clusteredLight{};
		uint32_t clusteredLights = 0;

		auto eyePositionOffset = eyePositionCached[0] - eyePositionCached[1];

		for (const auto& particleLight : currentParticleLights) {
			if (!particleLight.billboard) {
				auto particleSystem = static_cast<RE::NiParticleSystem*>(particleLight.node);
				if (particleSystem && particleSystem->GetParticleRuntimeData().particleData.get()) {
					// Process BSGeometry
					auto particleData = particleSystem->GetParticleRuntimeData().particleData.get();
					auto& particleSystemRuntimeData = particleSystem->GetParticleSystemRuntimeData();
					auto& particleRuntimeData = particleData->GetParticlesRuntimeData();

					auto numVertices = particleData->GetActiveVertexCount();
					for (std::uint32_t p = 0; p < numVertices; p++) {
						float radius = particleRuntimeData.radii[p] * particleRuntimeData.sizes[p];

						auto initialPosition = particleRuntimeData.positions[p];
						if (!particleSystemRuntimeData.isWorldspace) {
							// Detect first-person meshes
							if ((particleLight.node->GetModelData().modelBound.radius * particleLight.node->world.scale) != particleLight.node->worldBound.radius)
								initialPosition += particleLight.node->worldBound.center;
							else
								initialPosition += particleLight.node->world.translate;
						}

						RE::NiPoint3 positionWS = initialPosition - eyePositionCached[0];

						if (clusteredLights) {
							auto averageRadius = clusteredLight.radius / (float)clusteredLights;
							float radiusDiff = abs(averageRadius - radius);

							auto averagePosition = clusteredLight.positionWS[0].data / (float)clusteredLights;
							float positionDiff = positionWS.GetDistance({ averagePosition.x, averagePosition.y, averagePosition.z });

							if ((radiusDiff + positionDiff) > 32.0f || !settings.EnableParticleLightsOptimization) {
								clusteredLight.radius /= (float)clusteredLights;
								clusteredLight.positionWS[0].data /= (float)clusteredLights;
								clusteredLight.positionWS[1].data = clusteredLight.positionWS[0].data;
								if (eyeCount == 2) {
									clusteredLight.positionWS[1].data.x += eyePositionOffset.x / (float)clusteredLights;
									clusteredLight.positionWS[1].data.y += eyePositionOffset.y / (float)clusteredLights;
									clusteredLight.positionWS[1].data.z += eyePositionOffset.z / (float)clusteredLights;
								}

								clusteredLight.lightFlags.set(LightFlags::Simple);

								AddCachedParticleLights(lightsData, clusteredLight);

								clusteredLights = 0;
								clusteredLight.color = { 0, 0, 0 };
								clusteredLight.radius = 0;
								clusteredLight.positionWS[0].data = { 0, 0, 0 };
							}
						}

						if (particleRuntimeData.color) {
							float alpha = particleLight.color.alpha * particleRuntimeData.color[p].alpha;

							float3 color;
							color.x = particleLight.color.red * particleRuntimeData.color[p].red;
							color.y = particleLight.color.green * particleRuntimeData.color[p].green;
							color.z = particleLight.color.blue * particleRuntimeData.color[p].blue;

							clusteredLight.color += Saturation(color, settings.ParticleLightsSaturation) * alpha * settings.ParticleBrightness;
						} else {
							float alpha = particleLight.color.alpha;

							float3 color;
							color.x = particleLight.color.red;
							color.y = particleLight.color.green;
							color.z = particleLight.color.blue;

							clusteredLight.color += Saturation(color, settings.ParticleLightsSaturation) * alpha * settings.ParticleBrightness;
						}

						clusteredLight.radius += radius * particleLight.color.alpha * settings.ParticleRadius;

						clusteredLight.positionWS[0].data.x += positionWS.x;
						clusteredLight.positionWS[0].data.y += positionWS.y;
						clusteredLight.positionWS[0].data.z += positionWS.z;

						clusteredLights++;
					}
				}
			} else {
				// Process billboard
				LightData light{};

				light.color.x = particleLight.color.red;
				light.color.y = particleLight.color.green;
				light.color.z = particleLight.color.blue;

				light.color = Saturation(light.color, settings.ParticleLightsSaturation);

				light.color *= particleLight.color.alpha * settings.BillboardBrightness;
				light.radius = particleLight.node->worldBound.radius * particleLight.color.alpha * settings.BillboardRadius * 0.5f;

				auto position = particleLight.node->world.translate;

				SetLightPosition(light, position);  // Light is complete for both eyes by now

				light.lightFlags.set(LightFlags::Simple);

				AddCachedParticleLights(lightsData, light);
			}
		}

		if (clusteredLights) {
			clusteredLight.radius /= (float)clusteredLights;
			clusteredLight.positionWS[0].data /= (float)clusteredLights;
			clusteredLight.positionWS[1].data = clusteredLight.positionWS[0].data;
			if (eyeCount == 2) {
				clusteredLight.positionWS[1].data.x += eyePositionOffset.x / (float)clusteredLights;
				clusteredLight.positionWS[1].data.y += eyePositionOffset.y / (float)clusteredLights;
				clusteredLight.positionWS[1].data.z += eyePositionOffset.z / (float)clusteredLights;
			}
			clusteredLight.lightFlags.set(LightFlags::Simple);
			AddCachedParticleLights(lightsData, clusteredLight);
		}
	}

	auto context = globals::d3d::context;

	{
		auto projMatrixUnjittered = Util::GetCameraData(0).projMatrixUnjittered;
		float fov = atan(1.0f / static_cast<float4x4>(projMatrixUnjittered).m[0][0]) * 2.0f * (180.0f / 3.14159265359f);

		static float _lightsNear = 0.0f, _lightsFar = 0.0f, _fov = 0.0f;
		if (fabs(_fov - fov) > 1e-4 || fabs(_lightsNear - lightsNear) > 1e-4 || fabs(_lightsFar - lightsFar) > 1e-4) {
			LightBuildingCB updateData{};
			updateData.InvProjMatrix[0] = DirectX::XMMatrixInverse(nullptr, projMatrixUnjittered);
			if (eyeCount == 1)
				updateData.InvProjMatrix[1] = updateData.InvProjMatrix[0];
			else
				updateData.InvProjMatrix[1] = DirectX::XMMatrixInverse(nullptr, Util::GetCameraData(1).projMatrixUnjittered);
			updateData.LightsNear = lightsNear;
			updateData.LightsFar = lightsFar;

			lightBuildingCB->Update(updateData);

			ID3D11Buffer* buffer = lightBuildingCB->CB();
			context->CSSetConstantBuffers(0, 1, &buffer);

			ID3D11UnorderedAccessView* clusters_uav = clusters->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &clusters_uav, nullptr);

			context->CSSetShader(clusterBuildingCS, nullptr, 0);
			context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);

			ID3D11UnorderedAccessView* null_uav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

			_fov = fov;
			_lightsNear = lightsNear;
			_lightsFar = lightsFar;
		}
	}

	{
		lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(LightData) * lightCount;
		memcpy_s(mapped.pData, bytes, lightsData.data(), bytes);
		context->Unmap(lights->resource.get(), 0);

		LightCullingCB updateData{};
		updateData.LightCount = lightCount;
		lightCullingCB->Update(updateData);

		UINT counterReset[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightIndexCounter->uav.get(), counterReset);

		ID3D11Buffer* buffer = lightCullingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11ShaderResourceView* srvs[] = { clusters->srv.get(), lights->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { lightIndexCounter->uav.get(), lightIndexList->uav.get(), lightGrid->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(clusterCullingCS, nullptr, 0);
		context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr };
	context->CSSetShaderResources(0, 2, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[3] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 3, null_uavs, nullptr);
}

void LightLimitFix::Hooks::BSBatchRenderer_RenderPassImmediately::thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
{
	if (globals::features::lightLimitFix->CheckParticleLights(Pass, Technique))
		func(Pass, Technique, AlphaTest, RenderFlags);
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto singleton = globals::features::lightLimitFix;
	singleton->BSLightingShader_SetupGeometry_Before(Pass);
	func(This, Pass, RenderFlags);
	singleton->BSLightingShader_SetupGeometry_After(Pass);
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	auto singleton = globals::features::lightLimitFix;
	singleton->BSLightingShader_SetupGeometry_Before(Pass);
	singleton->BSLightingShader_SetupGeometry_After(Pass);
};

void LightLimitFix::Hooks::BSWaterShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	auto singleton = globals::features::lightLimitFix;
	singleton->BSLightingShader_SetupGeometry_Before(Pass);
	singleton->BSLightingShader_SetupGeometry_After(Pass);
};

float LightLimitFix::Hooks::AIProcess_CalculateLightValue_GetLuminance::thunk(RE::ShadowSceneNode* shadowSceneNode, RE::NiPoint3& targetPosition, int& numHits, float& sunLightLevel, float& lightLevel, RE::NiLight& refLight, int32_t shadowBitMask)
{
	auto ret = func(shadowSceneNode, targetPosition, numHits, sunLightLevel, lightLevel, refLight, shadowBitMask);
	globals::features::lightLimitFix->AddParticleLightLuminance(targetPosition, numHits, ret);
	return ret;
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights::thunk(RE::BSGraphics::PixelShader* PixelShader, RE::BSRenderPass* Pass, DirectX::XMMATRIX& Transform, uint32_t LightCount, uint32_t ShadowLightCount, float WorldScale, Space RenderSpace)
{
	globals::features::lightLimitFix->BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(Pass, Transform, LightCount, ShadowLightCount, WorldScale, RenderSpace);
	func(PixelShader, Pass, Transform, LightCount, ShadowLightCount, WorldScale, RenderSpace);
}

void LightLimitFix::Hooks::NiNode_Destroy::thunk(RE::NiNode* This)
{
	globals::features::lightLimitFix->CleanupParticleLights(This);
	func(This);
}