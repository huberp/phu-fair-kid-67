include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(phu_dsp_tests test_dsp.cpp)

target_include_directories(phu_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_dsp_tests PRIVATE cxx_std_17)

target_link_libraries(phu_dsp_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_circuit_tests
    CircuitLinearTests.cpp
    ../src/DSP/Circuit/Circuit.cpp
)

target_include_directories(phu_circuit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_circuit_tests PRIVATE cxx_std_17)

target_link_libraries(phu_circuit_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_nr_policy_tests
    NRPolicyTests.cpp
    ../src/DSP/Circuit/Nonlinear/NR.cpp
)

target_include_directories(phu_nr_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_nr_policy_tests PRIVATE cxx_std_17)

target_link_libraries(phu_nr_policy_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_triode_koren_tests
    TriodeKorenTests.cpp
    ../src/DSP/Circuit/Nonlinear/TriodeKoren.cpp
)

target_include_directories(phu_triode_koren_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_triode_koren_tests PRIVATE cxx_std_17)

target_link_libraries(phu_triode_koren_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_tube_stage_tests
    TubeStageTests.cpp
    ../src/DSP/Models/TubeStage.cpp
    ../src/DSP/Circuit/Nonlinear/TriodeKoren.cpp
)

target_include_directories(phu_tube_stage_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_tube_stage_tests PRIVATE cxx_std_17)

target_compile_definitions(phu_tube_stage_tests PRIVATE _USE_MATH_DEFINES)

target_link_libraries(phu_tube_stage_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_detector_tests
    DetectorTests.cpp
    ../src/DSP/Models/Sidechain/RectifierDetector.cpp
    ../src/DSP/Models/Sidechain/TimingNetwork.cpp
)

target_include_directories(phu_detector_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_detector_tests PRIVATE cxx_std_17)

target_compile_definitions(phu_detector_tests PRIVATE _USE_MATH_DEFINES)

target_link_libraries(phu_detector_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_variable_mu_tests
    VariableMuTests.cpp
    ../src/DSP/Models/Fairchild/VariableMuStage.cpp
    ../src/DSP/Circuit/Nonlinear/TriodeKoren.cpp
)

target_include_directories(phu_variable_mu_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_variable_mu_tests PRIVATE cxx_std_17)

target_compile_definitions(phu_variable_mu_tests PRIVATE _USE_MATH_DEFINES)

target_link_libraries(phu_variable_mu_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_fairchild670core_tests
    Fairchild670CoreTests.cpp
    ../src/DSP/Models/Fairchild/Fairchild670Core.cpp
    ../src/DSP/Models/Fairchild/VariableMuStage.cpp
    ../src/DSP/Models/Sidechain/RectifierDetector.cpp
    ../src/DSP/Models/Sidechain/TimingNetwork.cpp
    ../src/DSP/Circuit/Nonlinear/TriodeKoren.cpp
)

target_include_directories(phu_fairchild670core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_fairchild670core_tests PRIVATE cxx_std_17)

target_compile_definitions(phu_fairchild670core_tests PRIVATE _USE_MATH_DEFINES)

target_link_libraries(phu_fairchild670core_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_transformer_linear_tests
    TransformerLinearTests.cpp
    ../src/DSP/Models/Transformer/TransformerLinear.cpp
)

target_include_directories(phu_transformer_linear_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_transformer_linear_tests PRIVATE cxx_std_17)

target_compile_definitions(phu_transformer_linear_tests PRIVATE _USE_MATH_DEFINES)

target_link_libraries(phu_transformer_linear_tests PRIVATE Catch2::Catch2WithMain)

add_executable(phu_transformer_coupling_tests
    TransformerCouplingTests.cpp
    ../src/DSP/Circuit/Circuit.cpp
)

target_include_directories(phu_transformer_coupling_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(phu_transformer_coupling_tests PRIVATE cxx_std_17)

target_link_libraries(phu_transformer_coupling_tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(phu_dsp_tests)
catch_discover_tests(phu_circuit_tests)
catch_discover_tests(phu_nr_policy_tests)
catch_discover_tests(phu_triode_koren_tests)
catch_discover_tests(phu_tube_stage_tests)
catch_discover_tests(phu_detector_tests)
catch_discover_tests(phu_variable_mu_tests)
catch_discover_tests(phu_fairchild670core_tests)
catch_discover_tests(phu_transformer_linear_tests)
catch_discover_tests(phu_transformer_coupling_tests)
