cmake_minimum_required(VERSION 3.16)
project(perf_tutorial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认用 Release；-g 保证 perf 能解析到源码行
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-O2 -g -fno-omit-frame-pointer)

add_subdirectory(lessons/01_environment)
add_subdirectory(lessons/02_stat_basics)
add_subdirectory(lessons/03_memory_wait)
add_subdirectory(lessons/04_branch_miss)
add_subdirectory(lessons/05_hot_line)
add_subdirectory(lessons/06_callers)
add_subdirectory(lessons/07_flamegraph)
