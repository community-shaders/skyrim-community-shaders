#ifdef SHARC

#   ifndef SHARC_DEPENDENCY_HLSL
#       define SHARC_DEPENDENCY_HLSL

#       define SHARC_ENABLE_64_BIT_ATOMICS 0

#       define SHARC_UPDATE 1

#       ifndef SHARC_RESOLVE
#           define SHARC_RESOLVE 0
#       endif

#       include "Raytracing/Includes/RT/SHARC/SharcCommon.h"

#   endif // SHARC_DEPENDENCY_HLSL

#endif // SHARC