cmake_minimum_required(VERSION 3.20)
project(CameraLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenCV 4.7 REQUIRED COMPONENTS core imgproc calib3d videoio objdetect features2d imgcodecs)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Threads REQUIRED)

add_library(vision vision.cpp)
target_include_directories(vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(vision PUBLIC ${OpenCV_LIBS})

add_executable(camera-lab main.cpp camera.cpp video_view.cpp)
target_link_libraries(camera-lab PRIVATE vision Qt6::Widgets Qt6::OpenGLWidgets Threads::Threads)
target_compile_options(camera-lab PRIVATE -Wall -Wextra -Wpedantic)
qt_add_resources(camera-lab pattern-images PREFIX "/patterns" BASE "${CMAKE_CURRENT_SOURCE_DIR}/imgs"
    FILES imgs/chessboard.png imgs/asymmetric_circles.png imgs/charuco.png)

include(CTest)
if(BUILD_TESTING)
    add_executable(vision-test tests/vision_test.cpp camera.cpp)
    target_link_libraries(vision-test PRIVATE vision Qt6::Core Threads::Threads)
    target_compile_definitions(vision-test PRIVATE PATTERN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/imgs")
    add_test(NAME calibration-and-pose COMMAND vision-test)
endif()
