set(FFX_API_VK OFF)
set(FFX_API_DX12 OFF)
set(FFX_ALL OFF)
set(FFX_FSR3 ON)
set(FFX_FSR ON)
set(FFX_AUTO_COMPILE_SHADERS 1)

add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk)

# Upstream writes its libs to the shared <source>/bin/ffx_sdk, so presets with
# different IPO settings overwrite each other; a /GL lib then breaks the
# incremental Dev-Fast link (LNK4075, fatal under /WX). Keep them per-build.
foreach(
  _ffx_lib
  ffx_backend_dx11_x64
  ffx_fsr3_x64
  ffx_fsr3upscaler_x64
  ffx_frameinterpolation_x64
  ffx_opticalflow_x64
)
  set_target_properties(
    ${_ffx_lib}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/ffx_sdk"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/ffx_sdk"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/ffx_sdk"
  )
endforeach()

# Upstream bug: the FFX dx11 backend's compile_shaders() leaks literal
# out-variable names (e.g. "FSR2_PERMUTATION_OUTPUTS") into the dependency
# list of the phony ffx_shader_permutations_dx11 target. The VS generator
# ignores the bogus deps; Ninja fails with "missing and no known rule".
# Pre-creating empty placeholder files at those paths satisfies Ninja without
# triggering rebuilds. If an SDK update adds a new leaked name, add it here.
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_ffx_dx11_bindir
      "${CMAKE_BINARY_DIR}/extern/FidelityFX-SDK/sdk/src/backends/dx11"
  )
  foreach(
    _ffx_bogus_dep
    FSR1_PERMUTATION_OUTPUTS
    FSR2_PERMUTATION_OUTPUTS
    FSR3UPSCALER_PERMUTATION_OUTPUTS
    FRAMEINTERPOLATION_PERMUTATION_OUTPUTS
    OPTICALFLOW_PERMUTATION_OUTPUTS
  )
    if(NOT EXISTS "${_ffx_dx11_bindir}/${_ffx_bogus_dep}")
      file(
        WRITE "${_ffx_dx11_bindir}/${_ffx_bogus_dep}"
        "placeholder for upstream FFX CMake dependency-name leak; see cmake/FidelityFX-SDK.cmake\n"
      )
    endif()
  endforeach()
endif()

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  ffx_backend_dx11_x64
  ffx_fsr3_x64
)
