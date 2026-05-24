cmake_minimum_required(VERSION 3.15)
project(phu-fair-kid-67 VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PHU_BUILD_PLUGIN "Build the audio plugin (requires JUCE and platform audio SDKs)" ON)

enable_testing()

set(PHU_AUDIO_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/phu-audio-lib" CACHE PATH "Path to PhuAudioLib root")

if(PHU_BUILD_PLUGIN)
    # JUCE
    # Build JUCE extras/examples OFF for faster builds
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Build JUCE Extras")
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Build JUCE Examples")

    set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE root")
    if(NOT EXISTS "${JUCE_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE submodule not found. Run: git submodule update --init --recursive")
    endif()
    add_subdirectory("${JUCE_ROOT}")

    if(NOT EXISTS "${PHU_AUDIO_LIB_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "PhuAudioLib submodule not found. Run: git submodule update --init --recursive")
    endif()
    add_subdirectory("${PHU_AUDIO_LIB_ROOT}")

    # PhuNetworkLib's .cpp files include juce_core headers; link the JUCE
    # target that the parent project already provides, with matching defines.
    target_link_libraries(PhuNetworkLib PUBLIC juce::juce_core)
    target_compile_definitions(PhuNetworkLib PUBLIC
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
    )
    target_compile_definitions(PhuGLUtils PUBLIC
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
    )
    # Static libs linked into a shared library (VST3) need PIC
    set_target_properties(PhuNetworkLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set_target_properties(PhuGLUtils PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set_target_properties(PhuAnalogLib PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Fairchild model sources as a single static lib (JUCE-free; shared by plugin, tools, and tests)
    add_library(PhuFairchildModels STATIC
        src/DSP/Models/Fairchild/Fairchild670Core.cpp
        src/DSP/Models/Fairchild/VariableMuPushPullStage.cpp
        src/DSP/Models/Transformer/TransformerSecondOrder.cpp
        src/DSP/Models/Sidechain/TimingNetwork.cpp
    )
    target_include_directories(PhuFairchildModels PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PHU_AUDIO_LIB_ROOT}
    )
    target_compile_features(PhuFairchildModels PUBLIC cxx_std_17)
    target_compile_definitions(PhuFairchildModels PRIVATE _USE_MATH_DEFINES)
    target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
    set_target_properties(PhuFairchildModels PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Plugin support library
    add_library(PhuFairKidLib INTERFACE)
    target_link_libraries(PhuFairKidLib INTERFACE
        PhuAudioLib
        PhuNetworkLib
        PhuGLUtils
        PhuAnalogLib
        PhuFairchildModels
    )

    add_subdirectory(src)
    add_subdirectory(tools)
else()
    # Without JUCE, include only the DSP-only analog library (no network/GL dependencies).
    if(EXISTS "${PHU_AUDIO_LIB_ROOT}/analog/CMakeLists.txt")
        add_subdirectory("${PHU_AUDIO_LIB_ROOT}/analog")
    else()
        message(WARNING "phu-audio-lib analog directory not found; tests will not be built.")
    endif()
    if(TARGET PhuAnalogLib)
        add_library(PhuFairchildModels STATIC
            src/DSP/Models/Fairchild/Fairchild670Core.cpp
            src/DSP/Models/Fairchild/VariableMuPushPullStage.cpp
            src/DSP/Models/Transformer/TransformerSecondOrder.cpp
            src/DSP/Models/Sidechain/TimingNetwork.cpp
        )
        target_include_directories(PhuFairchildModels PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${PHU_AUDIO_LIB_ROOT}
        )
        target_compile_features(PhuFairchildModels PUBLIC cxx_std_17)
        target_compile_definitions(PhuFairchildModels PRIVATE _USE_MATH_DEFINES)
        target_link_libraries(PhuFairchildModels PUBLIC PhuAnalogLib)
    endif()
endif()

# Build DSP tests and calibration tool whenever PhuAnalogLib is available (no JUCE dependency).
# When PHU_BUILD_PLUGIN is ON, tools/ is already added inside that branch above.
if(TARGET PhuAnalogLib)
    add_subdirectory(tests)
    if(NOT PHU_BUILD_PLUGIN)
        add_subdirectory(tools)
    endif()
endif()
