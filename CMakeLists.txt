cmake_minimum_required(VERSION 3.15)
project(DotaBot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenCV из MSYS2
set(OpenCV_DIR "C:/msys64/ucrt64/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)

# ONNX Runtime 1.17.1
set(ONNXRUNTIME_DIR ${CMAKE_SOURCE_DIR}/deps/onnxruntime-win-x64-1.17.1)
include_directories(${ONNXRUNTIME_DIR}/include)
link_directories(${ONNXRUNTIME_DIR}/lib)

# nlohmann/json
include_directories(${CMAKE_SOURCE_DIR}/deps/json/include)

# cpp-httplib
include_directories(${CMAKE_SOURCE_DIR}/deps/cpp-httplib)

# Наши заголовки
include_directories(${CMAKE_SOURCE_DIR}/src)

# Sources
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_executable(DotaBot WIN32 ${SRC_FILES})

target_include_directories(DotaBot PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(DotaBot PRIVATE 
    ${OpenCV_LIBS} 
    onnxruntime 
    ws2_32
    comctl32
    d3d11 dxgi
    dwmapi
    user32 gdi32
)
