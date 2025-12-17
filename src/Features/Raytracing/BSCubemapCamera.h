#pragma once

#include "PCH.h"

class BSCubeMapCamera : public RE::NiCamera
{
public:
	virtual void UnkRender(UInt32 unk);
	enum etFace
	{
		FACE_POS_X = 0x0,
		FACE_NEG_X = 0x1,
		FACE_POS_Y = 0x2,
		FACE_NEG_Y = 0x3,
		FACE_POS_Z = 0x4,
		FACE_NEG_Z = 0x5,
		FACE_NUM = 0x6,
	};

	UInt32 uiLastUpdatedCamera;  // named for my own purposes
	BSRenderedTexturePtr spFaces[FACE_NUM];
	BSRenderedTexturePtr spTexture;
	ShadowSceneLight* pLight;
	NiNode* pSceneNode;
	NiPointer<BSShaderAccumulator> spAccumulator;

	static BSCubeMapCamera* CreateObject();
	static BSCubeMapCamera* Create(NiNode* apSceneNonde, float afViewDistance, UInt32 auiTextureSize = 0, D3DFORMAT aeFormat = D3DFMT_UNKNOWN);

	void SetupTransformation(UInt32 auiFace);

	void RenderCubeMap(NiTPointerListBase<NiAVObject*>* apNodeList, UInt32 auiMaxUpdates, UInt32 auiCullMode, bool abRenderWater = false);
};