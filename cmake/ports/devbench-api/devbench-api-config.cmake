get_filename_component(_DEVBENCH_API_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

# DevBench::API — header-only-ish target. The consumer gets the include dir and
# DevBenchAPI.cpp compiled into it (the consumer-side helper that fetches the
# interface via SKSE messaging). Link this, then call
# DevBenchAPI::GetDevBenchInterface001() after kPostLoad.
if(NOT TARGET DevBench::API)
    add_library(DevBench::API INTERFACE IMPORTED)
    set_target_properties(DevBench::API PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_DEVBENCH_API_DIR}/../../include"
        INTERFACE_SOURCES             "${_DEVBENCH_API_DIR}/src/DevBenchAPI.cpp"
    )
endif()
