#ifdef SHARC

#   ifndef SHARC_HELPER_DEPENDENCY_HLSL
#   define SHARC_HELPER_DEPENDENCY_HLSL

uint Hash(uint2 idx)
{
    return (idx.x * 73856093u) ^ (idx.y * 19349663u);
}

SharcParameters GetSharcParameters()
{
    SharcParameters sharcParameters;
    {
        sharcParameters.gridParameters.cameraPosition = Frame.Position;
        sharcParameters.gridParameters.sceneScale = Frame.SHaRC.SceneScale;
        sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
        sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

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