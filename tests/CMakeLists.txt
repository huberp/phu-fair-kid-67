include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Common include directories: project root + phu-audio-lib root (for analog/ headers)
set(PHU_TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../phu-audio-lib
)

add_executable(phu_dsp_tests test_dsp.cpp)
target_include_directories(phu_dsp_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_dsp_tests PRIVATE cxx_std_17)
target_link_libraries(phu_dsp_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_circuit_tests CircuitLinearTests.cpp)
target_include_directories(phu_circuit_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_circuit_tests PRIVATE cxx_std_17)
target_link_libraries(phu_circuit_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_nr_policy_tests NRPolicyTests.cpp)
target_include_directories(phu_nr_policy_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_nr_policy_tests PRIVATE cxx_std_17)
target_link_libraries(phu_nr_policy_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_triode_koren_tests TriodeKorenTests.cpp)
target_include_directories(phu_triode_koren_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_triode_koren_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_triode_koren_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_triode_koren_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_tube_stage_tests TubeStageTests.cpp)
target_include_directories(phu_tube_stage_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_tube_stage_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_tube_stage_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_tube_stage_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_detector_tests
    DetectorTests.cpp
)
target_include_directories(phu_detector_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_detector_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_detector_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_detector_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_variable_mu_tests VariableMuTests.cpp)
target_include_directories(phu_variable_mu_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_variable_mu_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_variable_mu_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_variable_mu_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

# ── New component tests ────────────────────────────────────────────────────────

add_executable(phu_variable_mu_push_pull_tests
    VariableMuPushPullTests.cpp
)
target_include_directories(phu_variable_mu_push_pull_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_variable_mu_push_pull_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_variable_mu_push_pull_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_variable_mu_push_pull_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_soft_rectifier_tests
    SoftRectifierDetectorTests.cpp
)
target_include_directories(phu_soft_rectifier_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_soft_rectifier_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_soft_rectifier_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_soft_rectifier_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transformer_second_order_tests
    TransformerSecondOrderTests.cpp
)
target_include_directories(phu_transformer_second_order_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_transformer_second_order_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_transformer_second_order_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_transformer_second_order_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

# ── Fairchild core tests (include new .cpp files) ─────────────────────────────

add_executable(phu_fairchild670core_tests
    Fairchild670CoreTests.cpp
)
target_include_directories(phu_fairchild670core_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_fairchild670core_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_fairchild670core_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_fairchild670core_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transformer_linear_tests TransformerLinearTests.cpp)
target_include_directories(phu_transformer_linear_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_transformer_linear_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_transformer_linear_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_transformer_linear_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_transformer_coupling_tests TransformerCouplingTests.cpp)
target_include_directories(phu_transformer_coupling_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_transformer_coupling_tests PRIVATE cxx_std_17)
target_link_libraries(phu_transformer_coupling_tests PRIVATE Catch2::Catch2WithMain PhuAnalogLib)

add_executable(phu_timing_conformance_tests
    TimingConformanceTests.cpp
)
target_include_directories(phu_timing_conformance_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_timing_conformance_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_timing_conformance_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_timing_conformance_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transfer_curve_tests
    TransferCurveTests.cpp
)
target_include_directories(phu_transfer_curve_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_transfer_curve_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_transfer_curve_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_transfer_curve_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

add_executable(phu_transfer_family_conformance_tests
    TransferFamilyConformanceTests.cpp
)
target_include_directories(phu_transfer_family_conformance_tests PRIVATE ${PHU_TEST_INCLUDE_DIRS})
target_compile_features(phu_transfer_family_conformance_tests PRIVATE cxx_std_17)
target_compile_definitions(phu_transfer_family_conformance_tests PRIVATE _USE_MATH_DEFINES)
target_link_libraries(phu_transfer_family_conformance_tests PRIVATE Catch2::Catch2WithMain PhuFairchildModels)

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
