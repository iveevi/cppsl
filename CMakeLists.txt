project(cppsl CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(features main.cpp)

target_link_libraries(features fmt)
