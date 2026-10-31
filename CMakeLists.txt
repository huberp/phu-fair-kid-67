cmake_minimum_required(VERSION 3.28)
project(phu-fair-kid-67 VERSION 0.1.0 LANGUAGES C CXX)

# =============================================================================
# Top-Level Guard
# =============================================================================
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

# =============================================================================
# Platform-Specific Settings
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJC OBJCXX)
endif()

# =============================================================================
# Global Settings (before fetching, so they also apply to fetched dependencies)
# =============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_PLUGIN "Build the audio plugin (requires JUCE and platform audio SDKs)" ON)
option(BUILD_TESTING "Build test executables" OFF)

if(BUILD_TESTING)
  enable_testing()
endif()

# =============================================================================
# Dependencies (fetched with FetchContent, see cmake/PhuDependencies.cmake)
# =============================================================================
# JUCE feature flags and build options; included before the fetch so that the
# options and compile definitions also reach JUCE and phu-audio-lib targets.
# The DSP-only build (BUILD_PLUGIN=OFF) neither fetches nor configures JUCE.
if(BUILD_PLUGIN)
    include(cmake/juce_config.cmake)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PhuDependencies)
if(BUILD_PLUGIN)
    phu_fetch_juce(TAG 8.0.15)
    phu_fetch_audio_lib(TAG main)
else()
    # phu-audio-lib's root project also defines the JUCE-dependent targets (PhuGLUtils),
    # so the DSP-only build adds only its analog/ subdirectory (PhuAnalogLib).
    FetchContent_Declare(
        phu-audio-lib
        GIT_REPOSITORY https://github.com/huberp/phu-audio-lib.git
        GIT_TAG        main
        GIT_SHALLOW    ON
        SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib
        SOURCE_SUBDIR  analog
    )
    FetchContent_MakeAvailable(phu-audio-lib)
endif()

# Warnings/sanitizers/coverage; included after the fetch so they only apply to this project
include(cmake/compiler_flags.cmake)

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
    # DSP-only: JUCE is not fetched, so PhuNetworkLib/PhuGLUtils are not usable.
    if(TARGET PhuAnalogLib)
        target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
    else()
        message(WARNING "PhuAnalogLib not found in phu-audio-lib; tests will not be built.")
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
