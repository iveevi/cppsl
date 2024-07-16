project(cppsl C CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(features
	main.cpp
	glad/src/gl.c)

include_directories(glad/include)

target_link_libraries(features fmt glfw)
