# =============================================================================
# Top-Level Guard
# =============================================================================
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Include Catch2 discovery module
include(Catch)

# Common include directories: project root + phu-audio-lib root (for analog/ headers)
# Using CMAKE_SOURCE_DIR for consistency
set(PHU_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/phu-audio-lib
)

# Helper function to create test targets with consistent settings
function(add_phu_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME}_tests ${SOURCE_FILE})
    target_include_directories(${TEST_NAME}_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
    target_compile_features(${TEST_NAME}_tests PRIVATE cxx_std_20)
    target_compile_definitions(${TEST_NAME}_tests PRIVATE _USE_MATH_DEFINES)
    
    # Link appropriate libraries based on test requirements
    if(TEST_NAME MATCHES "phu_dsp|phu_circuit|phu_nr_policy|phu_triode_koren|phu_tube_stage|phu_transformer_linear|phu_transformer_coupling")
        target_link_libraries(${TEST_NAME}_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)
    elseif(TEST_NAME MATCHES "phu_detector|phu_variable_mu_push_pull|phu_soft_rectifier|phu_transformer_second_order|phu_fairchild670core|phu_timing_conformance|phu_transfer_curve|phu_transfer_family_conformance")
        target_link_libraries(${TEST_NAME}_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)
    elseif(TEST_NAME MATCHES "phu_variable_mu")
        target_link_libraries(${TEST_NAME}_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)
    else()
        target_link_libraries(${TEST_NAME}_tests PRIVATE Catch2::Catch2WithMain)
    endif()
    
    # Discover tests using Catch2
    catch_discover_tests(${TEST_NAME}_tests)
endfunction()

# ---- DSP Tests ----
add_phu_test(phu_dsp test_dsp.cpp)

# ---- Circuit Tests ----
add_phu_test(phu_circuit CircuitLinearTests.cpp)
add_phu_test(phu_nr_policy NRPolicyTests.cpp)
add_phu_test(phu_triode_koren TriodeKorenTests.cpp)
add_phu_test(phu_tube_stage TubeStageTests.cpp)

# ---- Detector Tests ----
add_phu_test(phu_detector DetectorTests.cpp)

# ---- Variable Mu Tests ----
add_phu_test(phu_variable_mu VariableMuTests.cpp)

# ---- New component tests ----
add_phu_test(phu_variable_mu_push_pull VariableMuPushPullTests.cpp)
add_phu_test(phu_soft_rectifier SoftRectifierDetectorTests.cpp)
add_phu_test(phu_transformer_second_order TransformerSecondOrderTests.cpp)

# ---- Fairchild core tests (include new .cpp files) ----
add_phu_test(phu_fairchild670core Fairchild670CoreTests.cpp)

# ---- Transformer Tests ----
add_phu_test(phu_transformer_linear TransformerLinearTests.cpp)
add_phu_test(phu_transformer_coupling TransformerCouplingTests.cpp)

# ---- Timing and Transfer Tests ----
add_phu_test(phu_timing_conformance TimingConformanceTests.cpp)
add_phu_test(phu_transfer_curve TransferCurveTests.cpp)
add_phu_test(phu_transfer_family_conformance TransferFamilyConformanceTests.cpp)
