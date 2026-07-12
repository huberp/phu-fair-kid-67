# =============================================================================
# Top-Level Guard
# =============================================================================
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

add_executable(phu_calibrate
    calibrate.cpp
)

# Use CMAKE_SOURCE_DIR for consistency
target_include_directories(phu_calibrate PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/phu-audio-lib
)

target_compile_features(phu_calibrate PRIVATE cxx_std_20)
target_compile_definitions(phu_calibrate PRIVATE _USE_MATH_DEFINES)

# Link to PhuFairchildModels instead of hardcoding paths
target_link_libraries(phu_calibrate PRIVATE PhuFairchildModels)

# Install rule for the calibration tool
install(TARGETS phu_calibrate
    RUNTIME DESTINATION bin
)
