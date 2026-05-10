add_executable(phu_calibrate
    calibrate.cpp
    ../src/DSP/Models/Fairchild/Fairchild670Core.cpp
    ../src/DSP/Models/Sidechain/TimingNetwork.cpp
)

target_include_directories(phu_calibrate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../phu-audio-lib
)

target_compile_features(phu_calibrate PRIVATE cxx_std_17)

target_compile_definitions(phu_calibrate PRIVATE _USE_MATH_DEFINES)

target_link_libraries(phu_calibrate PRIVATE PhuAnalogLib)
