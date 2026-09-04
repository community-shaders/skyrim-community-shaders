cmake_minimum_required(VERSION 3.21)

add_library(IntelXeSS INTERFACE)

target_include_directories(
  IntelXeSS
  INTERFACE ${CMAKE_SOURCE_DIR}/extern/XeSS-SDK/inc
            ${CMAKE_SOURCE_DIR}/extern/XeSS-SDK/inc/xess
            ${CMAKE_SOURCE_DIR}/extern/XeSS-SDK/inc/xess_fg
            ${CMAKE_SOURCE_DIR}/extern/XeSS-SDK/inc/xell)
