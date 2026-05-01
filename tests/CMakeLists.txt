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

include(Catch)
catch_discover_tests(phu_dsp_tests)
catch_discover_tests(phu_circuit_tests)
catch_discover_tests(phu_nr_policy_tests)
catch_discover_tests(phu_triode_koren_tests)
