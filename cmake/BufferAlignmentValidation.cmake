# Buffer Alignment Validation for HLSL shaders
# This script validates that all GPU constant buffers are properly aligned to 16-byte boundaries

function(add_buffer_alignment_validation target_name)
    # Find Python interpreter
    find_package(Python3 COMPONENTS Interpreter QUIET)
    
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING "Python3 not found, skipping buffer alignment validation")
        return()
    endif()
    
    # Set up validation script path
    set(VALIDATOR_SCRIPT "${CMAKE_SOURCE_DIR}/tools/buffer_alignment_validator.py")
    
    if(NOT EXISTS "${VALIDATOR_SCRIPT}")
        message(WARNING "Buffer alignment validator script not found at ${VALIDATOR_SCRIPT}")
        return()
    endif()
    
    # Define shader directories to validate
    set(SHADER_DIRS
        "${CMAKE_SOURCE_DIR}/package/Shaders"
        "${CMAKE_SOURCE_DIR}/features"
    )
    
    # Create a custom target for buffer alignment validation
    add_custom_target(${target_name}_buffer_alignment_validation
        COMMAND ${Python3_EXECUTABLE} "${VALIDATOR_SCRIPT}" --strict ${SHADER_DIRS}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Validating GPU buffer alignment in HLSL shaders"
        VERBATIM
    )
    
    # Make the main target depend on validation
    add_dependencies(${target_name} ${target_name}_buffer_alignment_validation)
    
    # Create a separate target for fixing alignment issues
    add_custom_target(fix_buffer_alignment
        COMMAND ${Python3_EXECUTABLE} "${VALIDATOR_SCRIPT}" --fix ${SHADER_DIRS}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Fixing GPU buffer alignment issues in HLSL shaders"
        VERBATIM
    )
    
    # Create a target for just validation without strict mode
    add_custom_target(validate_buffer_alignment
        COMMAND ${Python3_EXECUTABLE} "${VALIDATOR_SCRIPT}" ${SHADER_DIRS}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Validating GPU buffer alignment (warnings only)"
        VERBATIM
    )
    
    message(STATUS "Buffer alignment validation enabled for target ${target_name}")
    message(STATUS "  - Use 'cmake --build . --target validate_buffer_alignment' to check alignment")
    message(STATUS "  - Use 'cmake --build . --target fix_buffer_alignment' to auto-fix issues")
endfunction()

# Option to enable/disable buffer alignment validation
option(ENABLE_BUFFER_ALIGNMENT_VALIDATION "Enable GPU buffer alignment validation during build" ON)

if(ENABLE_BUFFER_ALIGNMENT_VALIDATION)
    message(STATUS "GPU buffer alignment validation is enabled")
else()
    message(STATUS "GPU buffer alignment validation is disabled")
endif()