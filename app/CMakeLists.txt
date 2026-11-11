cmake_minimum_required(VERSION 3.20)
project(CameraLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc calib3d videoio)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Threads REQUIRED)

add_library(vision vision.cpp)
target_include_directories(vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(vision PUBLIC ${OpenCV_LIBS})

add_executable(camera-lab main.cpp camera.cpp video_view.cpp)
target_link_libraries(camera-lab PRIVATE vision Qt6::Widgets Qt6::OpenGLWidgets Threads::Threads)
target_compile_options(camera-lab PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(vision-test tests/vision_test.cpp camera.cpp)
    target_link_libraries(vision-test PRIVATE vision Qt6::Core Threads::Threads)
    add_test(NAME calibration-and-pose COMMAND vision-test)
endif()
