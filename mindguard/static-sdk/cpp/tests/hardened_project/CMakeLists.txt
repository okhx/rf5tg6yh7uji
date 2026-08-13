cmake_minimum_required(VERSION 3.28)
project(mindguard_hardened_e2e LANGUAGES CXX)

add_subdirectory(../.. mindguard-static)
add_executable(hardened_generated main.cpp)
mindguard_protect_cpp_target(hardened_generated
  PROJECT_ID e2e-project
  RELEASE_ID e2e-release
  SOURCES main.cpp)
add_executable(hardened_benchmark_4k benchmark_4k.cpp)
mindguard_protect_cpp_target(hardened_benchmark_4k
  PROJECT_ID e2e-project
  RELEASE_ID e2e-release
  SOURCES benchmark_4k.cpp)
