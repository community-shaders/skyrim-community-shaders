add_subdirectory(${CMAKE_SOURCE_DIR}/extern/CreationEngineRaytracing)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  CreationEngineRaytracing::CreationEngineRaytracing
)