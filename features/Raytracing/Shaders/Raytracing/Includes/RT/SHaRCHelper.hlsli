#ifdef SHARC

#   ifndef SHARC_HELPER_DEPENDENCY_HLSL
#   define SHARC_HELPER_DEPENDENCY_HLSL

#include "Common/Game.hlsli"
#include "Raytracing/Includes/Common.hlsli"

uint Hash(uint2 idx)
{
    return (idx.x * 73856093u) ^ (idx.y * 19349663u);
}

HashGridParameters GetSharcGridParameters()
{
    HashGridParameters gridParameters;
    {
        gridParameters.cameraPosition = Frame.Position;
        gridParameters.sceneScale = Frame.SHaRC.SceneScale * M_TO_GAME_UNIT;
        gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
        gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;
    }

    return gridParameters;
}

SharcParameters GetSharcParameters()
{
    SharcParameters sharcParameters;
    {
        sharcParameters.gridParameters = GetSharcGridParameters();

        sharcParameters.hashMapData.capacity = Frame.SHaRC.Capacity;
        sharcParameters.hashMapData.hashEntriesBuffer = u_SharcHashEntriesBuffer;

#if !SHARC_ENABLE_64_BIT_ATOMICS && !SHARC_RESOLVE
        sharcParameters.hashMapData.lockBuffer = u_SharcLockBuffer;
#endif // !SHARC_ENABLE_64_BIT_ATOMICS

        sharcParameters.accumulationBuffer = u_SharcAccumulationBuffer;
        sharcParameters.resolvedBuffer = u_SharcResolvedBuffer;
        sharcParameters.radianceScale = Frame.SHaRC.RadianceScale;
    }

    return sharcParameters;
}

SharcResolveParameters GetSharcResolveParameters()
{
    SharcResolveParameters resolveParameters;
    {
        resolveParameters.accumulationFrameNum = Frame.SHaRC.AccumFrameNum;
        resolveParameters.staleFrameNumMax = Frame.SHaRC.StaleFrameNum;
        resolveParameters.cameraPositionPrev = Frame.PositionPrev;
        resolveParameters.enableAntiFireflyFilter = Frame.SHaRC.AntifireflyFilter;
    }

    return resolveParameters;
}

#   endif // SHARC_HELPER_DEPENDENCY_HLSL

#endif // SHARC