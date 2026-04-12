cmake_minimum_required(VERSION 3.21)

add_library(NVAPI INTERFACE)

target_include_directories(
  NVAPI INTERFACE ${CMAKE_SOURCE_DIR}/extern/nvapi
)

target_link_libraries(
  NVAPI INTERFACE "${CMAKE_SOURCE_DIR}/extern/nvapi/amd64/nvapi64.lib"
)

if(MSVC)
  # NVAPI headers use Latin-1 encoding (© in copyright notices); suppress C4828
  # which fires when the compiler parses them as UTF-8.
  target_compile_options(NVAPI INTERFACE /wd4828)
endif()
