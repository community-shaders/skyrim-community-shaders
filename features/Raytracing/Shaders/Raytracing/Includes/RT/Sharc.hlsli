#ifdef SHARC

#   ifndef SHARC_COMMON_DEPENDENCY_HLSL
#   define SHARC_COMMON_DEPENDENCY_HLSL

#   define SHARC_ENABLE_64_BIT_ATOMICS 1
#   define SHARC_UPDATE 1

    #include "Raytracing/Includes/RT/SHARC/SharcCommon.h"

uint Hash(uint2 idx)
{
    return (idx.x * 73856093u) ^ (idx.y * 19349663u);
}

SharcParameters GetSharcParameters()
{
    SharcParameters sharcParameters;
    {
        sharcParameters.gridParameters.cameraPosition = Frame.Position;
        sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE * GAME_UNIT_TO_CM;
        sharcParameters.gridParameters.sceneScale = Frame.SHaRCScale;    
        sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

        sharcParameters.hashMapData.capacity = Frame.SHaRCCapacity;
        sharcParameters.hashMapData.hashEntriesBuffer = u_SharcHashEntriesBuffer;

#if !SHARC_ENABLE_64_BIT_ATOMICS
        sharcParameters.hashMapData.lockBuffer = u_HashCopyOffsetBuffer;
#endif // !SHARC_ENABLE_64_BIT_ATOMICS

        sharcParameters.radianceScale = 1e3f;
        sharcParameters.enableAntiFireflyFilter = false;   
    
        sharcParameters.accumulationBuffer = u_SharcAccumulationBuffer;
        sharcParameters.resolvedBuffer = u_SharcResolvedBuffer;
    } 

    return sharcParameters;
}

#   endif // SHARC_COMMON_DEPENDENCY_HLSL

#endif // SHARC