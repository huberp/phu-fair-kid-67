# =============================================================================
# Installation Targets
# =============================================================================

# Install PhuFairchildModels library
if(TARGET PhuFairchildModels)
    install(TARGETS PhuFairchildModels
        EXPORT PhuFairchildModelsTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
    )
endif()

# Install PhuFairKidLib interface library
if(TARGET PhuFairKidLib)
    install(TARGETS PhuFairKidLib
        EXPORT PhuFairKidLibTargets
    )
endif()

# Install headers
install(DIRECTORY src/DSP/ DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install plugin (if built)
if(BUILD_PLUGIN AND TARGET phu-fair-kid-67)
    install(TARGETS phu-fair-kid-67
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

# Install tests (optional)
if(BUILD_TESTING)
    install(DIRECTORY tests/ DESTINATION tests)
endif()

# Install tools
if(TARGET phu_calibrate)
    install(TARGETS phu_calibrate
        RUNTIME DESTINATION bin
    )
endif()

# =============================================================================
# Export Targets
# =============================================================================

if(TARGET PhuFairchildModels)
    install(EXPORT PhuFairchildModelsTargets
        FILE PhuFairchildModelsTargets.cmake
        NAMESPACE Phu::
        DESTINATION lib/cmake/PhuFairchildModels
    )
endif()

if(TARGET PhuFairKidLib)
    install(EXPORT PhuFairKidLibTargets
        FILE PhuFairKidLibTargets.cmake
        NAMESPACE Phu::
        DESTINATION lib/cmake/PhuFairKidLib
    )
endif()

# =============================================================================
# Package Configuration
# =============================================================================

include(CMakePackageConfigHelpers)

# Configure and install PhuFairchildModelsConfig.cmake
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/PhuFairchildModelsConfig.cmake
    INSTALL_DESTINATION lib/cmake/PhuFairchildModels
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/PhuFairchildModelsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/PhuFairchildModelsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/PhuFairchildModelsConfigVersion.cmake
    DESTINATION lib/cmake/PhuFairchildModels
)
