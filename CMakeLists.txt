cmake_minimum_required(VERSION 3.31.6)
project(phu-fair-kid-67 VERSION 0.1.0 LANGUAGES C CXX)

# =============================================================================
# Top-Level Guard
# =============================================================================
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

# =============================================================================
# Standard Settings
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Debug postfix
set(CMAKE_DEBUG_POSTFIX d)

# =============================================================================
# Platform-Specific Settings
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJC OBJCXX)
endif()

# =============================================================================
# Options
# =============================================================================
option(BUILD_PLUGIN "Build the audio plugin (requires JUCE and platform audio SDKs)" ON)
option(BUILD_TESTING "Build test executables" OFF)

if(BUILD_TESTING)
    enable_testing()
endif()

# =============================================================================
# Dependency Validation
# =============================================================================
foreach(SUBMODULE IN ITEMS JUCE phu-audio-lib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SUBMODULE}/CMakeLists.txt")
        message(FATAL_ERROR
            "${SUBMODULE} submodule not initialized. "
            "Run: git submodule update --init --recursive"
        )
    endif()
endforeach()

# =============================================================================
# Common Functions
# =============================================================================
include(cmake/functions.cmake)

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
set_target_properties(PhuFairchildModels PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)
phu_target_warnings(PhuFairchildModels)

# =============================================================================
# Plugin or Standalone Build
# =============================================================================
if(BUILD_PLUGIN)
    # JUCE
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Build JUCE Extras")
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Build JUCE Examples")

    add_subdirectory(JUCE)
    add_subdirectory(phu-audio-lib)

    # Link JUCE to phu-audio-lib targets
    if(TARGET PhuNetworkLib)
        target_link_libraries(PhuNetworkLib PUBLIC juce::juce_core)
        target_compile_definitions(PhuNetworkLib PUBLIC
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
        )
        set_target_properties(PhuNetworkLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
        phu_target_warnings(PhuNetworkLib)
    endif()

    if(TARGET PhuGLUtils)
        target_compile_definitions(PhuGLUtils PUBLIC
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
        )
        set_target_properties(PhuGLUtils PROPERTIES POSITION_INDEPENDENT_CODE ON)
        phu_target_warnings(PhuGLUtils)
    endif()

    if(TARGET PhuAnalogLib)
        set_target_properties(PhuAnalogLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
        phu_target_warnings(PhuAnalogLib)
    endif()

    # Link PhuFairchildModels to PhuAnalogLib if available
    if(TARGET PhuAnalogLib)
        target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
    endif()

    # Plugin support library
    add_library(PhuFairKidLib INTERFACE)
    target_link_libraries(PhuFairKidLib INTERFACE
        PhuAudioLib
        PhuAnalogLib
        PhuFairchildModels
    )

    add_subdirectory(src)
    add_subdirectory(tools)
else()
    # Without JUCE, include only the DSP-only analog library
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib/analog/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib/analog")
    else()
        message(WARNING "phu-audio-lib analog directory not found; tests will not be built.")
    endif()

    if(TARGET PhuAnalogLib)
        target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
        phu_target_warnings(PhuAnalogLib)
    endif()
endif()

# =============================================================================
# Tests
# =============================================================================
if(TARGET PhuAnalogLib AND BUILD_TESTING)
    add_subdirectory(tests)
    if(NOT BUILD_PLUGIN)
        add_subdirectory(tools)
    endif()
endif()

# =============================================================================
# Installation
# =============================================================================
if(PROJECT_IS_TOP_LEVEL)
    include(cmake/install.cmake)
endif()
