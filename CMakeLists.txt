cmake_minimum_required(VERSION 4.4.2)
project(onyx)
set(CMAKE_CXX_STANDARD 17)
add_executable(onyx)
target_sources(onyx
  PRIVATE
    main.cpp
)