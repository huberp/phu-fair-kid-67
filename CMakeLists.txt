cmake_minimum_required(VERSION 3.31.6)
project(phu-fair-kid-67 VERSION 0.1.0 LANGUAGES C CXX)

# =============================================================================
# Top-Level Guard
# =============================================================================
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

# =============================================================================
# Global Settings
# =============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/compiler_flags.cmake)

# =============================================================================
# Platform-Specific Settings
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJC OBJCXX)
endif()

# =============================================================================
# Standard Settings
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_PLUGIN "Build the audio plugin (requires JUCE and platform audio SDKs)" ON)
option(BUILD_TESTING "Build test executables" OFF)

if(BUILD_TESTING)
  enable_testing()
endif()

# =============================================================================
# Dependency Validation
# =============================================================================
# Validate submodules exist before any logic
foreach(SUBMODULE IN ITEMS JUCE phu-audio-lib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SUBMODULE}/CMakeLists.txt")
        message(FATAL_ERROR
            "${SUBMODULE} submodule not initialized. "
            "Run: git submodule update --init --recursive"
        )
    endif()
endforeach()

# =============================================================================
# PhuFairchildModels Library (defined once, used everywhere)
# =============================================================================
add_library(PhuFairchildModels STATIC
    src/DSP/Models/Fairchild/Fairchild670Core.cpp
    src/DSP/Models/Fairchild/VariableMuPushPullStage.cpp
    src/DSP/Models/Transformer/TransformerSecondOrder.cpp
    src/DSP/Models/Sidechain/TimingNetwork.cpp
)

target_include_directories(PhuFairchildModels PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib
)

target_compile_features(PhuFairchildModels PUBLIC cxx_std_20)
target_compile_definitions(PhuFairchildModels PRIVATE _USE_MATH_DEFINES)

# =============================================================================
# Install Rules
# =============================================================================
install(TARGETS PhuFairchildModels
    EXPORT PhuFairchildModelsTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)

install(DIRECTORY src/DSP/ DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# =============================================================================
# Plugin or Standalone Build
# =============================================================================
if(BUILD_PLUGIN)
    include(cmake/juce_config.cmake)
    add_subdirectory(JUCE)
    add_subdirectory(phu-audio-lib)

    # Link JUCE to phu-audio-lib targets
    if(TARGET PhuNetworkLib)
        target_link_libraries(PhuNetworkLib PUBLIC juce::juce_core)
    endif()

    # Link PhuFairchildModels to PhuAnalogLib if available
    if(TARGET PhuAnalogLib)
        target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
    endif()

    # Plugin support library
    add_library(PhuFairKidLib INTERFACE)
    target_link_libraries(PhuFairKidLib INTERFACE
        PhuAudioLib
        # PhuNetworkLib
        # PhuGLUtils
        PhuAnalogLib
        PhuFairchildModels
    )

    add_subdirectory(src)
    add_subdirectory(tools)
else()
    # Without JUCE, include only the DSP-only analog library (no network/GL dependencies).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib/analog/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib/analog")
    else()
        message(WARNING "phu-audio-lib analog directory not found; tests will not be built.")
    endif()
    
    if(TARGET PhuAnalogLib)
        target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
    endif()
endif()

# Build DSP tests and calibration tool whenever PhuAnalogLib is available (no JUCE dependency).
# When BUILD_PLUGIN is ON, tools/ is already added inside that branch above.
if(TARGET PhuAnalogLib AND BUILD_TESTING)
    add_subdirectory(tests)
    if(NOT BUILD_PLUGIN)
        add_subdirectory(tools)
    endif()
endif()
