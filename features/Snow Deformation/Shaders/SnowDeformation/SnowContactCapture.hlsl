cbuffer StaticCB : register(b1)
{
	float4 WorldRow0;
	float4 WorldRow1;
	float4 WorldRow2;

	float ObjectsDepth;
	float2 HeightWindowCenter;
	float HeightHalfExtent;
}

#ifdef SKINNED
cbuffer ContactSkinCB : register(b2)
{
	float4 BoneRows[240];
	float2 SkinWindowCenter;
	float SkinHalfExtent;
	float SkinBoneCount;
}

struct VS_INPUT_SKIN
{
	float4 Position : POSITION0;
	float4 BoneWeights : BLENDWEIGHT0;
	float4 BoneIndices : BLENDINDICES0;
};

float3x4 SkinnedMatrix(VS_INPUT_SKIN input)
{
	int4 rows = int4(765.01 * input.BoneIndices);
	float4 w = input.BoneWeights;
	const int lastRow = int(SkinBoneCount) * 3;
	w.x = rows.x < lastRow ? w.x : 0.0;
	w.y = rows.y < lastRow ? w.y : 0.0;
	w.z = rows.z < lastRow ? w.z : 0.0;
	w.w = rows.w < lastRow ? w.w : 0.0;
	const float wsum = dot(w, 1.0);
	[flatten] if (wsum < 1e-3)
	{
		w = float4(1.0, 0.0, 0.0, 0.0);
		rows = int4(0, 0, 0, 0);
	}
	else
		w /= wsum;
	return float3x4(BoneRows[rows.x], BoneRows[rows.x + 1], BoneRows[rows.x + 2]) * w.x +
	       float3x4(BoneRows[rows.y], BoneRows[rows.y + 1], BoneRows[rows.y + 2]) * w.y +
	       float3x4(BoneRows[rows.z], BoneRows[rows.z + 1], BoneRows[rows.z + 2]) * w.z +
	       float3x4(BoneRows[rows.w], BoneRows[rows.w + 1], BoneRows[rows.w + 2]) * w.w;
}

float3 SkinnedWorld(VS_INPUT_SKIN input)
{
	return mul(float4(input.Position.xyz, 1.0), transpose(SkinnedMatrix(input)));
}
#endif

struct VS_INPUT
{
	float4 Position : POSITION0;
	float4 Normal : NORMAL0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float WorldZ : TEXCOORD0;
};

#	ifdef VSHADER
#		ifdef SKINNED
VS_OUTPUT main(VS_INPUT_SKIN input)
{
	float3 worldAbs = SkinnedWorld(input);

	float2 ndc = (worldAbs.xy - SkinWindowCenter) / SkinHalfExtent;

	VS_OUTPUT output;
	output.Position = float4(ndc, 0.5, 1.0);
	output.WorldZ = worldAbs.z;
	return output;
}
#		else
VS_OUTPUT main(VS_INPUT input)
{
	float3 posMS = input.Position.xyz;
	float3 worldAbs = float3(
		dot(WorldRow0.xyz, posMS) + WorldRow0.w,
		dot(WorldRow1.xyz, posMS) + WorldRow1.w,
		dot(WorldRow2.xyz, posMS) + WorldRow2.w);

	float2 ndc = (worldAbs.xy - HeightWindowCenter) / HeightHalfExtent;

	VS_OUTPUT output;
	output.Position = float4(ndc, 0.5, 1.0);
	output.WorldZ = worldAbs.z;
	return output;
}
#		endif
#	endif

#	ifdef PSHADER
float2 main(VS_OUTPUT input) : SV_Target
{
	return input.WorldZ.xx;
}
#	endif
