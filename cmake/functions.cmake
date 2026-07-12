# =============================================================================
# Compiler Warning Functions
# =============================================================================

function(phu_target_warnings TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE
            /W4
            /WX
            /permissive-
        )
    else()
        target_compile_options(${TARGET} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
        )
    endif()
endfunction()

# =============================================================================
# IDE Folder Organization
# =============================================================================

function(phu_set_folder TARGET FOLDER)
    set_target_properties(${TARGET} PROPERTIES FOLDER ${FOLDER})
endfunction()
