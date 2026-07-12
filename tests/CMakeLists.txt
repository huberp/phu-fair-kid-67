include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Common include directories: project root + phu-audio-lib root (for analog/ headers)
# Use absolute paths based on CMAKE_CURRENT_SOURCE_DIR
set(PHU_TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../phu-audio-lib
)

# Test helper library to reduce duplication
add_library(PhuTestHelpers INTERFACE)
target_include_directories(PhuTestHelpers INTERFACE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(PhuTestHelpers INTERFACE cxx_std_20)
target_compile_definitions(PhuTestHelpers INTERFACE _USE_MATH_DEFINES)

# Individual test executables
add_executable(phu_dsp_tests test_dsp.cpp)
target_link_libraries(phu_dsp_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain)

add_executable(phu_circuit_tests CircuitLinearTests.cpp)
target_link_libraries(phu_circuit_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_nr_policy_tests NRPolicyTests.cpp)
target_link_libraries(phu_nr_policy_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_triode_koren_tests TriodeKorenTests.cpp)
target_link_libraries(phu_triode_koren_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_tube_stage_tests TubeStageTests.cpp)
target_link_libraries(phu_tube_stage_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_detector_tests DetectorTests.cpp)
target_link_libraries(phu_detector_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_variable_mu_tests VariableMuTests.cpp)
target_link_libraries(phu_variable_mu_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

# —— New component tests ————————————————————————————————————————————————

add_executable(phu_variable_mu_push_pull_tests VariableMuPushPullTests.cpp)
target_link_libraries(phu_variable_mu_push_pull_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_soft_rectifier_tests SoftRectifierDetectorTests.cpp)
target_link_libraries(phu_soft_rectifier_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transformer_second_order_tests TransformerSecondOrderTests.cpp)
target_link_libraries(phu_transformer_second_order_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

# —— Fairchild core tests ———————————————————————————————————————————————

add_executable(phu_fairchild670core_tests Fairchild670CoreTests.cpp)
target_link_libraries(phu_fairchild670core_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transformer_linear_tests TransformerLinearTests.cpp)
target_link_libraries(phu_transformer_linear_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_transformer_coupling_tests TransformerCouplingTests.cpp)
target_link_libraries(phu_transformer_coupling_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_timing_conformance_tests TimingConformanceTests.cpp)
target_link_libraries(phu_timing_conformance_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transfer_curve_tests TransferCurveTests.cpp)
target_link_libraries(phu_transfer_curve_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transfer_family_conformance_tests TransferFamilyConformanceTests.cpp)
target_link_libraries(phu_transfer_family_conformance_tests PRIVATE PhuTestHelpers Catch2::Catch2WithMain PhuFairchildModels)

# Test discovery
include(Catch)
catch_discover_tests(phu_dsp_tests)
catch_discover_tests(phu_circuit_tests)
catch_discover_tests(phu_nr_policy_tests)
catch_discover_tests(phu_triode_koren_tests)
catch_discover_tests(phu_tube_stage_tests)
catch_discover_tests(phu_detector_tests)
catch_discover_tests(phu_variable_mu_tests)
catch_discover_tests(phu_variable_mu_push_pull_tests)
catch_discover_tests(phu_soft_rectifier_tests)
catch_discover_tests(phu_transformer_second_order_tests)
catch_discover_tests(phu_fairchild670core_tests)
catch_discover_tests(phu_transformer_linear_tests)
catch_discover_tests(phu_transformer_coupling_tests)
catch_discover_tests(phu_timing_conformance_tests)
catch_discover_tests(phu_transfer_curve_tests)
catch_discover_tests(phu_transfer_family_conformance_tests)
