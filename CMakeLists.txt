cmake_minimum_required(VERSION 3.16)
project(onyx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) 
add_executable(onyx main.cpp)
target_compile_options(onyx 
PRIVATE -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(onyx
PRIVATE Threads::Threads)