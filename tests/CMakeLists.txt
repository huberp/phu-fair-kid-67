add_executable(test_placeholder test_placeholder.cpp)

target_include_directories(test_placeholder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../phu-audio-lib
)

target_compile_features(test_placeholder PRIVATE cxx_std_17)

enable_testing()
add_test(NAME placeholder_tests COMMAND test_placeholder)
